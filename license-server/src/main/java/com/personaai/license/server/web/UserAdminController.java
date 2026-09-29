package com.personaai.license.server.web;

import java.util.Map;

import jakarta.servlet.http.HttpServletRequest;

import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.security.core.session.SessionRegistry;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.servlet.mvc.support.RedirectAttributes;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.security.AppPrincipal;
import com.personaai.license.server.security.ClientIp;
import com.personaai.license.server.security.ReauthService;
import com.personaai.license.server.user.AppUser;
import com.personaai.license.server.user.UserRepository;
import com.personaai.license.server.user.UserService;

@Controller
public class UserAdminController {
    private final UserRepository users;
    private final UserService userService;
    private final ReauthService reauth;
    private final AuditService audit;
    private final SessionRegistry sessions;
    private final ViewFormat fmt;
    private final java.time.Clock clock;

    public UserAdminController(UserRepository users, UserService userService, ReauthService reauth, AuditService audit,
                               SessionRegistry sessions, ViewFormat fmt, java.time.Clock clock) {
        this.fmt = fmt;
        this.clock = clock;
        this.users = users;
        this.userService = userService;
        this.reauth = reauth;
        this.audit = audit;
        this.sessions = sessions;
    }

    @GetMapping("/users")
    public String list(Model model) {
        model.addAttribute("users", users.findAll());
        model.addAttribute("roleValues", AppUser.Role.values());
        model.addAttribute("now", clock.instant());
        java.time.Duration ttl = userService.tempPasswordTtl();
        // 1시간 단위로 나누어떨어지지 않으면(예: 30분) 분으로 표시 — "0시간" 표시 방지
        model.addAttribute("tempTtlText", ttl.toMinutes() % 60 == 0 ? ttl.toHours() + "시간" : ttl.toMinutes() + "분");
        return "users";
    }

    private boolean reauthOk(AppPrincipal p, String password, String totp, RedirectAttributes ra) {
        if (reauth.verify(p, password, totp)) return true;
        ra.addFlashAttribute("error", "재인증에 실패했습니다.");
        return false;
    }

    /** 대상 사용자의 모든 세션을 즉시 끝낸다(비활성화·비밀번호/TOTP 초기화 후). */
    private void expireSessions(long userId) {
        for (Object principal : sessions.getAllPrincipals()) {
            if (principal instanceof AppPrincipal ap && ap.id() == userId) {
                sessions.getAllSessions(principal, false).forEach(s -> s.expireNow());
            }
        }
    }

    @PostMapping("/users")
    public String create(@RequestParam String username, @RequestParam AppUser.Role role, @RequestParam String tempPassword,
                         @RequestParam String password, @RequestParam String totp, @AuthenticationPrincipal AppPrincipal p,
                         HttpServletRequest req, RedirectAttributes ra) {
        if (!reauthOk(p, password, totp, ra)) return "redirect:/users";
        try {
            java.time.Instant expires = userService.create(username, role, tempPassword);
            audit.record(p.getUsername(), "USER_CREATE", username, ClientIp.of(req),
                    Map.of("role", role.name(), "temp_password_expires_at", expires.toString()));
            ra.addFlashAttribute("info", username + " 계정을 만들었습니다. 임시 비밀번호는 " + fmt.kst(expires)
                    + " 까지 유효하며, 첫 로그인 때 비밀번호 변경과 OTP 등록을 합니다.");
        } catch (UserService.PolicyException e) {
            ra.addFlashAttribute("error", e.getMessage());
        }
        return "redirect:/users";
    }

    @PostMapping("/users/{id}/enabled")
    public String setEnabled(@PathVariable long id, @RequestParam boolean enabled, @RequestParam String password,
                             @RequestParam String totp, @AuthenticationPrincipal AppPrincipal p,
                             HttpServletRequest req, RedirectAttributes ra) {
        if (!reauthOk(p, password, totp, ra)) return "redirect:/users";
        AppUser target = users.findById(id).orElseThrow();
        if (!enabled && target.id() == p.id()) {
            ra.addFlashAttribute("error", "자기 계정은 비활성화할 수 없습니다.");
        } else if (!enabled && target.role() == AppUser.Role.ADMIN && target.enabled() && users.countEnabledAdmins() <= 1) {
            ra.addFlashAttribute("error", "마지막 관리자 계정은 비활성화할 수 없습니다.");
        } else {
            users.setEnabled(id, enabled);
            if (!enabled) expireSessions(id);
            audit.record(p.getUsername(), enabled ? "USER_ENABLE" : "USER_DISABLE", target.username(), ClientIp.of(req), Map.of());
            ra.addFlashAttribute("info", target.username() + (enabled ? " 활성화" : " 비활성화") + " 완료");
        }
        return "redirect:/users";
    }

    @PostMapping("/users/{id}/reset-password")
    public String resetPassword(@PathVariable long id, @RequestParam String tempPassword, @RequestParam String password,
                                @RequestParam String totp, @AuthenticationPrincipal AppPrincipal p,
                                HttpServletRequest req, RedirectAttributes ra) {
        if (!reauthOk(p, password, totp, ra)) return "redirect:/users";
        AppUser target = users.findById(id).orElseThrow();
        try {
            java.time.Instant expires = userService.resetPassword(id, tempPassword);
            expireSessions(id);
            audit.record(p.getUsername(), "USER_RESET_PASSWORD", target.username(), ClientIp.of(req),
                    Map.of("temp_password_expires_at", expires.toString()));
            ra.addFlashAttribute("info", target.username() + " 비밀번호를 초기화했습니다. 임시 비밀번호는 "
                    + fmt.kst(expires) + " 까지 유효합니다(다음 로그인 때 변경).");
        } catch (UserService.PolicyException e) {
            ra.addFlashAttribute("error", e.getMessage());
        }
        return "redirect:/users";
    }

    @PostMapping("/users/{id}/reset-totp")
    public String resetTotp(@PathVariable long id, @RequestParam String password, @RequestParam String totp,
                            @AuthenticationPrincipal AppPrincipal p, HttpServletRequest req, RedirectAttributes ra) {
        if (!reauthOk(p, password, totp, ra)) return "redirect:/users";
        AppUser target = users.findById(id).orElseThrow();
        userService.resetTotp(id);
        expireSessions(id);
        audit.record(p.getUsername(), "USER_RESET_TOTP", target.username(), ClientIp.of(req), Map.of());
        ra.addFlashAttribute("info", target.username() + " OTP 를 초기화했습니다(다음 로그인 때 재등록).");
        return "redirect:/users";
    }
}

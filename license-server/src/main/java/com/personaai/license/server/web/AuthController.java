package com.personaai.license.server.web;

import java.net.URLEncoder;
import java.nio.charset.StandardCharsets;
import java.util.Map;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import jakarta.servlet.http.HttpSession;

import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.stereotype.Controller;
import org.springframework.ui.Model;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestParam;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.security.AppPrincipal;
import com.personaai.license.server.security.AuthStages;
import com.personaai.license.server.security.ClientIp;
import com.personaai.license.server.user.AppUser;
import com.personaai.license.server.user.UserRepository;
import com.personaai.license.server.user.UserService;

@Controller
public class AuthController {
    static final String PENDING_TOTP = "auth.pendingTotpSecret";
    static final String MFA_TRIES = "auth.mfaTries";
    static final int MAX_MFA_TRIES = 5;

    private final UserRepository users;
    private final UserService userService;
    private final AuthStages stages;
    private final AuditService audit;

    public AuthController(UserRepository users, UserService userService, AuthStages stages, AuditService audit) {
        this.users = users;
        this.userService = userService;
        this.stages = stages;
        this.audit = audit;
    }

    @GetMapping("/login")
    public String login() {
        return "login";
    }

    @GetMapping("/mfa")
    public String mfa() {
        return "mfa";
    }

    @PostMapping("/mfa")
    public String mfaSubmit(@AuthenticationPrincipal AppPrincipal p, @RequestParam String code,
                            HttpServletRequest req, HttpServletResponse res, Model model) {
        AppUser u = users.findById(p.id()).orElseThrow();
        if (userService.verifyTotp(u, code)) {
            stages.markMfaDone(req);
            audit.record(p.getUsername(), "LOGIN_OK", null, ClientIp.of(req), Map.of());
            return "redirect:" + stages.advance(p, req, res);
        }
        HttpSession s = req.getSession();
        int tries = (s.getAttribute(MFA_TRIES) instanceof Integer i ? i : 0) + 1;
        s.setAttribute(MFA_TRIES, tries);
        audit.record(p.getUsername(), "MFA_FAIL", null, ClientIp.of(req), Map.of("tries", tries));
        if (tries >= MAX_MFA_TRIES) {
            userService.recordFailure(u.username());
            s.invalidate();
            return "redirect:/login?error";
        }
        model.addAttribute("error", "인증 코드가 올바르지 않습니다.");
        return "mfa";
    }

    @GetMapping("/account/password")
    public String passwordForm() {
        return "account-password";
    }

    @PostMapping("/account/password")
    public String passwordSubmit(@AuthenticationPrincipal AppPrincipal p, @RequestParam String current,
                                 @RequestParam String next, @RequestParam String confirm,
                                 HttpServletRequest req, HttpServletResponse res, Model model) {
        if (!next.equals(confirm)) {
            model.addAttribute("error", "새 비밀번호 확인이 일치하지 않습니다.");
            return "account-password";
        }
        try {
            userService.changePassword(users.findById(p.id()).orElseThrow(), current, next);
        } catch (UserService.TempPasswordExpiredException e) {
            // 로그인은 만료 전에 했지만 변경 시점에 만료됨 → 세션 종료(계획서 §13 T3)
            audit.record(p.getUsername(), "PASSWORD_CHANGE_DENIED", p.getUsername(), ClientIp.of(req),
                    Map.of("reason", "temp password expired"));
            req.getSession().invalidate();
            return "redirect:/login?tempExpired";
        } catch (UserService.PolicyException e) {
            model.addAttribute("error", e.getMessage());
            return "account-password";
        }
        audit.record(p.getUsername(), "PASSWORD_CHANGE", p.getUsername(), ClientIp.of(req), Map.of());
        return "redirect:" + stages.advance(p, req, res);
    }

    @GetMapping("/account/totp")
    public String totpForm(@AuthenticationPrincipal AppPrincipal p, HttpSession session, Model model) {
        String secret = (String) session.getAttribute(PENDING_TOTP);
        if (secret == null) {
            secret = UserService.newTotpSecret();
            session.setAttribute(PENDING_TOTP, secret);
        }
        String label = URLEncoder.encode("LicenseServer:" + p.getUsername(), StandardCharsets.UTF_8).replace("+", "%20");
        model.addAttribute("secret", secret.replaceAll("(.{4})", "$1 ").strip());
        model.addAttribute("uri", "otpauth://totp/" + label + "?secret=" + secret
                + "&issuer=LicenseServer&algorithm=SHA1&digits=6&period=30");
        return "account-totp";
    }

    @PostMapping("/account/totp")
    public String totpSubmit(@AuthenticationPrincipal AppPrincipal p, @RequestParam String code, HttpSession session,
                             HttpServletRequest req, HttpServletResponse res, Model model) {
        String secret = (String) session.getAttribute(PENDING_TOTP);
        AppUser u = users.findById(p.id()).orElseThrow();
        if (secret == null || !userService.enrollTotp(u, secret, code)) {
            model.addAttribute("error", "인증 코드가 올바르지 않습니다. 앱에 등록한 뒤 표시된 6자리를 입력하세요.");
            return totpForm(p, session, model);
        }
        session.removeAttribute(PENDING_TOTP);
        stages.markMfaDone(req); // 방금 두 번째 요소 소유를 증명함
        audit.record(p.getUsername(), "TOTP_ENROLL", p.getUsername(), ClientIp.of(req), Map.of());
        audit.record(p.getUsername(), "LOGIN_OK", null, ClientIp.of(req), Map.of());
        return "redirect:" + stages.advance(p, req, res);
    }
}

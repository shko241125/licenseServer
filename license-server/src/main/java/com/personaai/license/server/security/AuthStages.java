package com.personaai.license.server.security;

import java.io.IOException;
import java.util.List;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import jakarta.servlet.http.HttpSession;

import org.springframework.security.authentication.UsernamePasswordAuthenticationToken;
import org.springframework.security.core.Authentication;
import org.springframework.security.core.GrantedAuthority;
import org.springframework.security.core.authority.SimpleGrantedAuthority;
import org.springframework.security.core.context.SecurityContext;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.security.web.context.SecurityContextRepository;
import org.springframework.stereotype.Component;

import com.personaai.license.server.user.AppUser;
import com.personaai.license.server.user.UserRepository;

/**
 * 로그인 이후 단계: 비밀번호 변경(강제) → TOTP 등록(미등록 시) 또는 TOTP 확인 → 전체 권한.
 * 중간 단계에서는 STAGE_* 권한 하나만 가지므로 다른 화면에 접근할 수 없다. 단계가 오를 때 세션 ID 를 바꾼다.
 */
@Component
public class AuthStages {
    public static final String PWCHANGE = "STAGE_PWCHANGE";
    public static final String TOTP_ENROLL = "STAGE_TOTP_ENROLL";
    public static final String MFA = "STAGE_MFA";
    static final String MFA_DONE = "auth.mfaDone";

    private final UserRepository users;
    private final SecurityContextRepository contextRepository;
    private final org.springframework.security.core.session.SessionRegistry sessionRegistry;

    public AuthStages(UserRepository users, SecurityContextRepository contextRepository,
                      org.springframework.security.core.session.SessionRegistry sessionRegistry) {
        this.users = users;
        this.contextRepository = contextRepository;
        this.sessionRegistry = sessionRegistry;
    }

    /** 현재 사용자의 다음 단계로 인증 객체를 바꾸고 이동할 경로를 돌려준다. */
    public String advance(AppPrincipal principal, HttpServletRequest req, HttpServletResponse res) {
        AppUser u = users.findById(principal.id()).orElseThrow();
        HttpSession session = req.getSession();
        boolean mfaDone = Boolean.TRUE.equals(session.getAttribute(MFA_DONE));
        String stage;
        String target;
        if (u.mustChangePassword()) {
            stage = PWCHANGE;
            target = "/account/password";
        } else if (!u.totpEnrolled()) {
            stage = TOTP_ENROLL;
            target = "/account/totp";
        } else if (!mfaDone) {
            stage = MFA;
            target = "/mfa";
        } else {
            stage = null;
            target = "/";
        }
        List<GrantedAuthority> authorities = stage == null ? List.of(principal.roleAuthority())
                : List.of(new SimpleGrantedAuthority(stage));
        Authentication auth = UsernamePasswordAuthenticationToken.authenticated(principal, null, authorities);
        // 권한이 바뀌는 시점마다 세션 ID 를 바꿔 세션 고정 공격을 막는다. 동시 세션 레지스트리는 컨테이너의
        // 세션 ID 변경 이벤트에만 기대지 않고 직접 갱신한다(갱신이 빠지면 계정 비활성화 시 세션을 끊지 못함).
        String oldId = session.getId();
        String newId = req.changeSessionId();
        sessionRegistry.removeSessionInformation(oldId);
        sessionRegistry.registerNewSession(newId, principal);
        SecurityContext ctx = SecurityContextHolder.createEmptyContext();
        ctx.setAuthentication(auth);
        SecurityContextHolder.setContext(ctx);
        contextRepository.saveContext(ctx, req, res);
        return target;
    }

    public void markMfaDone(HttpServletRequest req) {
        req.getSession().setAttribute(MFA_DONE, Boolean.TRUE);
    }

    public static void redirect(HttpServletResponse res, HttpServletRequest req, String path) throws IOException {
        res.sendRedirect(req.getContextPath() + path);
    }
}

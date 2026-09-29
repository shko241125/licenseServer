package com.personaai.license.server.security;

import java.io.IOException;
import java.time.Clock;
import java.time.Duration;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import jakarta.servlet.http.HttpSession;

import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.web.filter.OncePerRequestFilter;

/** 활동과 무관하게 로그인 후 일정 시간(기본 8시간)이 지나면 세션을 끝낸다. */
public class AbsoluteSessionTimeoutFilter extends OncePerRequestFilter {
    static final String STARTED = "auth.sessionStartedAt";
    private final Clock clock;
    private final Duration max;

    public AbsoluteSessionTimeoutFilter(Clock clock, Duration max) {
        this.clock = clock;
        this.max = max;
    }

    @Override
    protected void doFilterInternal(HttpServletRequest req, HttpServletResponse res, FilterChain chain)
            throws ServletException, IOException {
        HttpSession s = req.getSession(false);
        // 세션 시작 시각은 컨테이너의 getCreationTime()(시스템 시계) 대신 주입한 Clock 으로 직접 기록한다.
        // 한 판정에 두 시계를 섞으면 시계가 어긋날 때(테스트 시계, 시계 보정) 오판한다.
        Long started = s == null ? null : (Long) s.getAttribute(STARTED);
        if (s != null && started == null) {
            s.setAttribute(STARTED, clock.millis());
        } else if (s != null && clock.millis() - started > max.toMillis()) {
            s.invalidate();
            SecurityContextHolder.clearContext();
            res.sendRedirect(req.getContextPath() + "/login?expired");
            return;
        }
        chain.doFilter(req, res);
    }
}

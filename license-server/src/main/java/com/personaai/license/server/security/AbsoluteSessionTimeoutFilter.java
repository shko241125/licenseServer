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
        if (s != null && clock.millis() - s.getCreationTime() > max.toMillis()) {
            s.invalidate();
            SecurityContextHolder.clearContext();
            res.sendRedirect(req.getContextPath() + "/login?expired");
            return;
        }
        chain.doFilter(req, res);
    }
}

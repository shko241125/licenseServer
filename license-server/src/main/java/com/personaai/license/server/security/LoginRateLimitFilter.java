package com.personaai.license.server.security;

import java.io.IOException;
import java.time.Clock;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.Deque;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.springframework.web.filter.OncePerRequestFilter;

/** IP 별 로그인 시도 속도 제한(계정 잠금과 별개로, 여러 계정을 돌며 시도하는 공격 대응). 단일 인스턴스 메모리. */
public class LoginRateLimitFilter extends OncePerRequestFilter {
    static final int LIMIT = 20;
    static final Duration WINDOW = Duration.ofMinutes(5);

    private final Clock clock;
    private final Map<String, Deque<Long>> attempts = new ConcurrentHashMap<>();

    public LoginRateLimitFilter(Clock clock) {
        this.clock = clock;
    }

    @Override
    protected boolean shouldNotFilter(HttpServletRequest req) {
        // getServletPath() 는 서블릿 매핑에 따라 빈 문자열일 수 있어(그러면 제한이 조용히 꺼짐) URI 로 판단한다.
        String path = req.getRequestURI().substring(req.getContextPath().length());
        return !("POST".equals(req.getMethod()) && ("/login".equals(path) || "/mfa".equals(path)));
    }

    @Override
    protected void doFilterInternal(HttpServletRequest req, HttpServletResponse res, FilterChain chain)
            throws ServletException, IOException {
        long now = clock.millis();
        long cutoff = now - WINDOW.toMillis();
        Deque<Long> q = attempts.computeIfAbsent(ClientIp.of(req), k -> new ArrayDeque<>());
        boolean blocked;
        synchronized (q) {
            while (!q.isEmpty() && q.peekFirst() < cutoff) q.pollFirst();
            blocked = q.size() >= LIMIT;
            if (!blocked) q.addLast(now);
        }
        if (attempts.size() > 10_000) attempts.values().removeIf(d -> { synchronized (d) { return d.isEmpty() || d.peekLast() < cutoff; } });
        if (blocked) {
            res.sendError(429, "Too many login attempts");
            return;
        }
        chain.doFilter(req, res);
    }
}

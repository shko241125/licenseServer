package com.personaai.license.server.security;

import java.time.Clock;
import java.util.Map;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.event.EventListener;
import org.springframework.security.access.hierarchicalroles.RoleHierarchy;
import org.springframework.security.access.hierarchicalroles.RoleHierarchyImpl;
import org.springframework.security.authentication.event.AuthenticationFailureBadCredentialsEvent;
import org.springframework.security.authentication.event.AuthenticationFailureLockedEvent;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.core.session.SessionRegistry;
import org.springframework.security.core.session.SessionRegistryImpl;
import org.springframework.security.core.userdetails.UserDetailsService;
import org.springframework.security.core.userdetails.UsernameNotFoundException;
import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.security.web.SecurityFilterChain;
import org.springframework.security.web.authentication.UsernamePasswordAuthenticationFilter;
import org.springframework.security.web.context.HttpSessionSecurityContextRepository;
import org.springframework.security.web.context.SecurityContextRepository;
import org.springframework.security.web.header.writers.ReferrerPolicyHeaderWriter;
import org.springframework.security.web.session.HttpSessionEventPublisher;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.config.LicenseProperties;
import com.personaai.license.server.user.UserRepository;
import com.personaai.license.server.user.UserService;

@Configuration
public class SecurityConfig {
    static final String CSP = "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; "
            + "form-action 'self'; frame-ancestors 'none'; base-uri 'none'; object-src 'none'";

    @Bean
    SecurityFilterChain filterChain(HttpSecurity http, AuthStages stages, AuditService audit,
                                    SessionRegistry sessionRegistry, SecurityContextRepository contextRepository,
                                    Clock clock, LicenseProperties props) throws Exception {
        String anyRole = "hasAnyRole('ADMIN','ISSUER','VIEWER')";
        http
            .securityContext(c -> c.securityContextRepository(contextRepository))
            .authorizeHttpRequests(a -> a
                .requestMatchers("/login", "/css/**", "/favicon.ico", "/error", "/error/**").permitAll()
                // 헬스체크: 관리 포트(컨테이너 내부 127.0.0.1:8081)에만 매핑된다. 보안 체인은 관리 포트에도 적용되므로 명시 허용.
                .requestMatchers("/actuator/health").permitAll()
                .requestMatchers("/account/password").hasAnyAuthority(AuthStages.PWCHANGE, "ROLE_VIEWER", "ROLE_ISSUER", "ROLE_ADMIN")
                .requestMatchers("/account/totp").hasAuthority(AuthStages.TOTP_ENROLL)
                .requestMatchers("/mfa").hasAuthority(AuthStages.MFA)
                .requestMatchers("/licenses/new", "/licenses/preview", "/licenses/edit", "/licenses/issue").hasRole("ISSUER")
                .requestMatchers("/licenses/*/void", "/keys/unseal", "/keys/seal", "/audit/**", "/users/**").hasRole("ADMIN")
                .anyRequest().hasRole("VIEWER"))  // 역할 계층: ADMIN > ISSUER > VIEWER
            .formLogin(f -> f
                .loginPage("/login")
                .successHandler((req, res, auth) -> {
                    AppPrincipal p = (AppPrincipal) auth.getPrincipal();
                    audit.record(p.getUsername(), "LOGIN_PASSWORD_OK", null, ClientIp.of(req), Map.of());
                    AuthStages.redirect(res, req, stages.advance(p, req, res));
                })
                .failureHandler((req, res, ex) -> {
                    String name = String.valueOf(req.getParameter("username"));
                    audit.record(name.length() > 64 ? name.substring(0, 64) : name, "LOGIN_FAIL", null,
                            ClientIp.of(req), Map.of("reason", ex.getClass().getSimpleName()));
                    // 원인과 무관하게 같은 메시지. 예외: 임시 비밀번호 만료는 비밀번호가 맞은 뒤에만 판정되므로
                    // 안내해도 계정 존재 여부가 새지 않는다(계획서 §13 T2).
                    boolean tempExpired = ex instanceof org.springframework.security.authentication.CredentialsExpiredException;
                    res.sendRedirect(req.getContextPath() + (tempExpired ? "/login?tempExpired" : "/login?error"));
                }))
            .logout(l -> l.logoutUrl("/logout").logoutSuccessUrl("/login?logout")
                .deleteCookies("__Host-LSID").invalidateHttpSession(true))
            .sessionManagement(s -> s
                .sessionFixation(f -> f.changeSessionId())
                .maximumSessions(1).sessionRegistry(sessionRegistry).expiredUrl("/login?expired"))
            .exceptionHandling(e -> e.accessDeniedPage("/error/403"))
            .addFilterBefore(new LoginRateLimitFilter(clock), UsernamePasswordAuthenticationFilter.class)
            .addFilterBefore(new AbsoluteSessionTimeoutFilter(clock, props.sessionAbsoluteTimeout()),
                    UsernamePasswordAuthenticationFilter.class)
            .headers(h -> h
                .contentSecurityPolicy(c -> c.policyDirectives(CSP))
                .frameOptions(f -> f.deny())
                .referrerPolicy(r -> r.policy(ReferrerPolicyHeaderWriter.ReferrerPolicy.NO_REFERRER))
                .httpStrictTransportSecurity(t -> t.includeSubDomains(true).maxAgeInSeconds(31_536_000)));
        return http.build();
    }

    @Bean
    UserDetailsService userDetailsService(UserRepository users, Clock clock) {
        return username -> users.findByUsername(username)
                .map(u -> new AppPrincipal(u, u.lockedAt(clock.instant()), u.tempPasswordExpiredAt(clock.instant())))
                .orElseThrow(() -> new UsernameNotFoundException("not found"));
    }

    @Bean
    PasswordEncoder passwordEncoder() {
        return new BCryptPasswordEncoder(12);
    }

    @Bean
    RoleHierarchy roleHierarchy() {
        return RoleHierarchyImpl.fromHierarchy("ROLE_ADMIN > ROLE_ISSUER\nROLE_ISSUER > ROLE_VIEWER");
    }

    @Bean
    SessionRegistry sessionRegistry() {
        return new SessionRegistryImpl();
    }

    @Bean
    HttpSessionEventPublisher httpSessionEventPublisher() {
        return new HttpSessionEventPublisher();
    }

    @Bean
    SecurityContextRepository securityContextRepository() {
        return new HttpSessionSecurityContextRepository();
    }

    /** 잘못된 비밀번호 → 실패 카운터(5회면 15분 잠금). 존재하지 않는 사용자명은 DB 에 영향 없음. */
    @Bean
    AuthEventListener authEventListener(UserService userService, AuditService audit) {
        return new AuthEventListener(userService, audit);
    }

    public static class AuthEventListener {
        private final UserService userService;
        private final AuditService audit;

        AuthEventListener(UserService userService, AuditService audit) {
            this.userService = userService;
            this.audit = audit;
        }

        @EventListener
        public void onBadCredentials(AuthenticationFailureBadCredentialsEvent e) {
            String username = String.valueOf(e.getAuthentication().getPrincipal());
            if (userService.recordFailure(username)) {
                audit.record(username, "ACCOUNT_LOCKED", username, null, Map.of("minutes", UserService.LOCK.toMinutes()));
            }
        }

        @EventListener
        public void onLocked(AuthenticationFailureLockedEvent e) {
            // 잠긴 동안의 시도는 카운트하지 않는다(잠금 연장 공격으로 정상 사용자를 계속 막지 않게).
        }

        @EventListener
        public void onSuccess(org.springframework.security.authentication.event.AuthenticationSuccessEvent e) {
            if (e.getAuthentication().getPrincipal() instanceof AppPrincipal p) userService.recordSuccess(p.id());
        }
    }
}

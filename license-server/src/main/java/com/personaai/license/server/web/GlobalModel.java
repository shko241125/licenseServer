package com.personaai.license.server.web;

import java.util.HashSet;
import java.util.Set;

import org.springframework.security.core.Authentication;
import org.springframework.security.core.GrantedAuthority;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.ControllerAdvice;
import org.springframework.web.bind.annotation.ModelAttribute;

import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.security.AppPrincipal;

import jakarta.servlet.http.HttpServletRequest;

/**
 * 모든 화면 공통 모델: 현재 사용자, 화면 표시용 역할(계층 반영), 현재 경로(메뉴 강조), 서명 키 상태(사이드바).
 * 권한 판정 자체는 SecurityConfig 가 한다.
 */
@ControllerAdvice
public class GlobalModel {
    private final KeyVault vault;

    public GlobalModel(KeyVault vault) {
        this.vault = vault;
    }

    @ModelAttribute("currentPath")
    public String currentPath(HttpServletRequest req) {
        return req.getRequestURI().substring(req.getContextPath().length());
    }

    @ModelAttribute("navKeyState")
    public KeyVault.State navKeyState() {
        return vault.state();
    }

    @ModelAttribute("me")
    public AppPrincipal me(@AuthenticationPrincipal AppPrincipal p) {
        return p;
    }

    @ModelAttribute("roles")
    public Set<String> roles(Authentication auth) {
        Set<String> r = new HashSet<>();
        if (auth == null) return r;
        for (GrantedAuthority a : auth.getAuthorities()) r.add(a.getAuthority());
        if (r.contains("ROLE_ADMIN")) r.add("ROLE_ISSUER");
        if (r.contains("ROLE_ISSUER")) r.add("ROLE_VIEWER");
        return r;
    }
}

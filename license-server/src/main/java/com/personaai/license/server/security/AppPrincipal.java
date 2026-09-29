package com.personaai.license.server.security;

import java.util.Collection;
import java.util.List;

import org.springframework.security.core.GrantedAuthority;
import org.springframework.security.core.authority.SimpleGrantedAuthority;
import org.springframework.security.core.userdetails.UserDetails;

import com.personaai.license.server.user.AppUser;

/** 세션에 저장되는 최소 정보. 비밀번호 해시는 인증 직후 지운다(eraseCredentials). */
public final class AppPrincipal implements UserDetails, org.springframework.security.core.CredentialsContainer {
    private final long id;
    private final String username;
    private final AppUser.Role role;
    private final boolean enabled;
    private final boolean locked;
    private String passwordHash;

    public AppPrincipal(AppUser u, boolean locked) {
        this.id = u.id();
        this.username = u.username();
        this.role = u.role();
        this.enabled = u.enabled();
        this.locked = locked;
        this.passwordHash = u.passwordHash();
    }

    public long id() {
        return id;
    }

    public AppUser.Role role() {
        return role;
    }

    public GrantedAuthority roleAuthority() {
        return new SimpleGrantedAuthority("ROLE_" + role.name());
    }

    @Override
    public Collection<? extends GrantedAuthority> getAuthorities() {
        return List.of(roleAuthority());
    }

    @Override
    public String getPassword() {
        return passwordHash;
    }

    @Override
    public String getUsername() {
        return username;
    }

    @Override
    public boolean isAccountNonLocked() {
        return !locked;
    }

    @Override
    public boolean isEnabled() {
        return enabled;
    }

    @Override
    public void eraseCredentials() {
        passwordHash = null;
    }

    // SessionRegistry(동시 세션 제한)는 principal 의 equals/hashCode 로 사용자를 구분한다.
    @Override
    public boolean equals(Object o) {
        return o instanceof AppPrincipal p && p.id == id;
    }

    @Override
    public int hashCode() {
        return Long.hashCode(id);
    }
}

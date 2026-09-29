package com.personaai.license.server.security;

import org.springframework.stereotype.Service;

import com.personaai.license.server.user.AppUser;
import com.personaai.license.server.user.UserRepository;
import com.personaai.license.server.user.UserService;

/** 민감 작업(키 해제·재봉인, 무효 처리, 계정 관리) 직전 비밀번호 + TOTP 재확인. 실패는 로그인 실패로 센다. */
@Service
public class ReauthService {
    private final UserRepository users;
    private final UserService userService;

    public ReauthService(UserRepository users, UserService userService) {
        this.users = users;
        this.userService = userService;
    }

    public boolean verify(AppPrincipal principal, String password, String totp) {
        AppUser u = users.findById(principal.id()).orElseThrow();
        boolean ok = userService.passwordMatches(u, password) && userService.verifyTotp(u, totp);
        if (!ok) userService.recordFailure(u.username());
        return ok;
    }
}

package com.personaai.license.server.security;

import jakarta.servlet.http.HttpServletRequest;

/** 리버스 프록시를 두지 않으므로 X-Forwarded-For 등 헤더는 신뢰하지 않는다(위조 가능). */
public final class ClientIp {
    private ClientIp() {}

    public static String of(HttpServletRequest req) {
        return req.getRemoteAddr();
    }
}

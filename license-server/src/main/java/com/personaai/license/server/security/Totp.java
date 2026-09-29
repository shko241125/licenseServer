package com.personaai.license.server.security;

import java.nio.ByteBuffer;
import java.security.GeneralSecurityException;
import java.time.Instant;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/** RFC 6238 TOTP (HMAC-SHA1, 30초, 6자리) — 일반 OTP 앱(Google/Microsoft Authenticator 등) 호환 기본값. */
public final class Totp {
    public static final int PERIOD_SECONDS = 30;
    public static final int DIGITS = 6;
    public static final int SECRET_BYTES = 20;

    private Totp() {}

    public static long step(Instant t) {
        return Math.floorDiv(t.getEpochSecond(), PERIOD_SECONDS);
    }

    public static String code(byte[] secret, long step, int digits) {
        try {
            Mac mac = Mac.getInstance("HmacSHA1");
            mac.init(new SecretKeySpec(secret, "HmacSHA1"));
            byte[] h = mac.doFinal(ByteBuffer.allocate(8).putLong(step).array());
            int off = h[h.length - 1] & 0x0F;
            int bin = ((h[off] & 0x7F) << 24) | ((h[off + 1] & 0xFF) << 16) | ((h[off + 2] & 0xFF) << 8) | (h[off + 3] & 0xFF);
            int mod = (int) Math.pow(10, digits);
            return String.format("%0" + digits + "d", bin % mod);
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException(e);
        }
    }

    /** 현재 step 과 앞뒤 1 step(±30초 시계 오차)을 허용. 일치한 step 을 반환, 없으면 -1. */
    public static long match(byte[] secret, String code, Instant now) {
        if (code == null || !code.matches("\\d{" + DIGITS + "}")) return -1;
        long s = step(now);
        long found = -1;
        for (long c = s - 1; c <= s + 1; c++) {
            // 시간 차 누설을 줄이려고 끝까지 비교한다
            if (java.security.MessageDigest.isEqual(code(secret, c, DIGITS).getBytes(), code.getBytes())) found = c;
        }
        return found;
    }
}

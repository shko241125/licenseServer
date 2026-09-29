package com.personaai.license.server.user;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.GeneralSecurityException;
import java.time.Clock;
import java.time.Duration;
import java.util.Base64;
import java.util.List;
import java.util.Locale;

import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import com.personaai.license.server.config.LicenseProperties;
import com.personaai.license.server.crypto.Base32;
import com.personaai.license.server.crypto.Crypto;
import com.personaai.license.server.security.Totp;

@Service
public class UserService {
    public static final int MIN_PASSWORD = 12;
    public static final int MAX_PASSWORD = 128;
    public static final int HISTORY = 3;
    public static final int MAX_FAILURES = 5;
    public static final Duration LOCK = Duration.ofMinutes(15);

    private final UserRepository users;
    private final PasswordEncoder encoder;
    private final Clock clock;
    private final byte[] dataKey;

    public UserService(UserRepository users, PasswordEncoder encoder, Clock clock, LicenseProperties props) {
        this.users = users;
        this.encoder = encoder;
        this.clock = clock;
        this.dataKey = loadDataKey(props);
    }

    /** TOTP 비밀값 암호화 키(32바이트 Base64). 없으면 기동 실패 — TOTP 는 필수 기능(D-S1). */
    private static byte[] loadDataKey(LicenseProperties props) {
        try {
            byte[] k = Base64.getDecoder().decode(Files.readString(props.dataKeyFile(), StandardCharsets.US_ASCII).strip());
            if (k.length != 32) throw new IllegalStateException("data key must be 32 bytes (Base64)");
            return k;
        } catch (java.io.IOException | IllegalArgumentException e) {
            throw new IllegalStateException("cannot read data key file " + props.dataKeyFile()
                    + " (create with: openssl rand -base64 32)", e);
        }
    }

    public static class PolicyException extends RuntimeException {
        public PolicyException(String message) {
            super(message);
        }
    }

    public static void checkPolicy(String username, String password) {
        if (password == null || password.length() < MIN_PASSWORD || password.length() > MAX_PASSWORD) {
            throw new PolicyException("비밀번호는 " + MIN_PASSWORD + "~" + MAX_PASSWORD + "자여야 합니다.");
        }
        if (password.toLowerCase(Locale.ROOT).contains(username.toLowerCase(Locale.ROOT))) {
            throw new PolicyException("비밀번호에 사용자명을 포함할 수 없습니다.");
        }
        if (password.chars().distinct().count() < 4) {
            throw new PolicyException("비밀번호에 서로 다른 문자를 4종 이상 사용하세요.");
        }
    }

    @Transactional
    public long create(String username, AppUser.Role role, String tempPassword) {
        if (!username.matches("^[a-z0-9._-]{3,32}$")) {
            throw new PolicyException("사용자명은 소문자·숫자·._- 3~32자여야 합니다.");
        }
        if (users.findByUsername(username).isPresent()) throw new PolicyException("이미 있는 사용자명입니다.");
        checkPolicy(username, tempPassword);
        String hash = encoder.encode(tempPassword);
        long id = users.insert(username, hash, role);
        users.updatePassword(id, hash, true); // 이력에 남기고 첫 로그인 때 변경 강제
        return id;
    }

    @Transactional
    public void changePassword(AppUser user, String current, String next) {
        if (!encoder.matches(current, user.passwordHash())) throw new PolicyException("현재 비밀번호가 올바르지 않습니다.");
        checkPolicy(user.username(), next);
        List<String> recent = users.recentPasswordHashes(user.id(), HISTORY);
        if (recent.stream().anyMatch(h -> encoder.matches(next, h))) {
            throw new PolicyException("최근 " + HISTORY + "개 비밀번호는 다시 쓸 수 없습니다.");
        }
        users.updatePassword(user.id(), encoder.encode(next), false);
    }

    @Transactional
    public void resetPassword(long userId, String tempPassword) {
        AppUser u = users.findById(userId).orElseThrow();
        checkPolicy(u.username(), tempPassword);
        users.updatePassword(userId, encoder.encode(tempPassword), true);
    }

    public boolean passwordMatches(AppUser user, String raw) {
        return raw != null && encoder.matches(raw, user.passwordHash());
    }

    /** 잘못된 비밀번호 1회. 잠기면 true. 없는 사용자명이면 아무것도 하지 않는다. */
    public boolean recordFailure(String username) {
        return users.recordFailure(username, MAX_FAILURES, clock.instant().plus(LOCK));
    }

    public void recordSuccess(long userId) {
        users.resetFailures(userId);
    }

    // ---- TOTP ----

    public static String newTotpSecret() {
        return Base32.encode(Crypto.randomBytes(Totp.SECRET_BYTES));
    }

    /** 등록: 사용자가 방금 앱에 넣은 비밀값으로 만든 코드가 맞으면 암호화해 저장. */
    @Transactional
    public boolean enrollTotp(AppUser user, String secretBase32, String code) {
        byte[] secret = Base32.decode(secretBase32);
        long step = Totp.match(secret, code, clock.instant());
        if (step < 0) return false;
        byte[] aad = ("totp:" + user.id()).getBytes(StandardCharsets.UTF_8);
        String enc = Base64.getEncoder().encodeToString(Crypto.gcmEncrypt(dataKey, secret, aad));
        users.setTotp(user.id(), enc, step);
        return true;
    }

    /** 로그인·재인증용 TOTP 확인. 같은 코드(step) 재사용은 거부. */
    public boolean verifyTotp(AppUser user, String code) {
        if (!user.totpEnrolled()) return false;
        byte[] secret;
        try {
            byte[] aad = ("totp:" + user.id()).getBytes(StandardCharsets.UTF_8);
            secret = Crypto.gcmDecrypt(dataKey, Base64.getDecoder().decode(user.totpSecretEnc()), aad);
        } catch (GeneralSecurityException e) {
            return false;
        }
        long step = Totp.match(secret, code, clock.instant());
        java.util.Arrays.fill(secret, (byte) 0);
        return step >= 0 && users.consumeTotpStep(user.id(), step);
    }

    @Transactional
    public void resetTotp(long userId) {
        users.setTotp(userId, null, 0);
    }
}

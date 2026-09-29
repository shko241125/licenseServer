package com.personaai.license.server.user;

import java.time.Instant;

public record AppUser(long id, String username, String passwordHash, Role role, String totpSecretEnc,
                      long totpLastStep, boolean enabled, boolean mustChangePassword, int failedCount,
                      Instant lockedUntil, Instant createdAt, Instant passwordExpiresAt) {

    public enum Role { ADMIN, ISSUER, VIEWER }

    public boolean totpEnrolled() {
        return totpSecretEnc != null;
    }

    /** 임시 비밀번호(변경 필요)이고 만료 시각이 지났는가. 만료 시각이 없으면(부트스트랩 등) 만료되지 않는다. */
    public boolean tempPasswordExpiredAt(Instant now) {
        return mustChangePassword && passwordExpiresAt != null && !now.isBefore(passwordExpiresAt);
    }

    public boolean lockedAt(Instant now) {
        return lockedUntil != null && lockedUntil.isAfter(now);
    }
}

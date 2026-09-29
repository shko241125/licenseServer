package com.personaai.license.server.user;

import java.time.Instant;

public record AppUser(long id, String username, String passwordHash, Role role, String totpSecretEnc,
                      long totpLastStep, boolean enabled, boolean mustChangePassword, int failedCount,
                      Instant lockedUntil, Instant createdAt) {

    public enum Role { ADMIN, ISSUER, VIEWER }

    public boolean totpEnrolled() {
        return totpSecretEnc != null;
    }

    public boolean lockedAt(Instant now) {
        return lockedUntil != null && lockedUntil.isAfter(now);
    }
}

package com.personaai.license.server.license;

import java.time.Instant;
import java.util.UUID;

public record LicenseRecord(
        long id, String licenseId, UUID submissionToken, Status status, String projectName, String licenseType,
        String siteId, int onlineStt, int offlineStt, Instant issuedAt, Instant notBefore, Instant notAfter,
        int gracePeriodDays, String warningNotice, String licenseText, String licenseSha256, String publicKeyFp,
        String renewedFrom, long issuedBy, String issuedByName, String error, String voidReason, Instant voidedAt,
        Instant createdAt) {

    public enum Status { PENDING, ISSUED, FAILED, VOID }

    public Instant graceUntil() {
        return notAfter.plusSeconds(gracePeriodDays * 86_400L);
    }

    /** 파일 이름에 쓸 수 없는 문자는 '_' 로. */
    public String downloadFileName() {
        return siteId.replaceAll("[^A-Za-z0-9_-]", "_") + "_" + licenseId + ".lic";
    }
}

package com.personaai.license.server.license;

import java.time.Instant;
import java.util.Map;
import java.util.UUID;

/**
 * 발급 장부 1행. 두 형식이 공존한다(DB CHECK license_format_shape 로 한 행은 한 형식만):
 *  - license_ver 1: licenseVer = 1, solution·channelsTotal·channels(항목 → 세부 항목 → 상한) 사용
 *  - 이전 형식(format_version 1, 2026-10-06 이전 발급분): licenseVer = null, onlineStt·offlineStt·gracePeriodDays 사용
 */
public record LicenseRecord(
        long id, String licenseId, UUID submissionToken, Status status, String projectName, String licenseType,
        String siteId, Instant issuedAt, Instant notBefore, Instant notAfter, String warningNotice,
        Integer licenseVer, String solution, Integer channelsTotal,
        Map<String, Map<String, Integer>> channels,
        Integer onlineStt, Integer offlineStt, Integer gracePeriodDays,
        String licenseText, String licenseSha256, String publicKeyFp,
        String renewedFrom, long issuedBy, String issuedByName, String error, String voidReason, Instant voidedAt,
        Instant createdAt) {

    public enum Status { PENDING, ISSUED, FAILED, VOID }

    /** 이전 형식(format_version) 행. 새 SDK 는 이 파일을 받지 않는다(LICENSE_UNSUPPORTED_VERSION). */
    public boolean legacy() {
        return licenseVer == null;
    }

    /** 운영 영구 표기(9999-12-31T23:59:59Z). */
    public boolean permanent() {
        return Contract.PERMANENT.equals(notAfter);
    }

    /** 이전 형식 행의 유예 종료 시각. 새 형식에는 유예가 없어 null. */
    public Instant graceUntil() {
        return legacy() && gracePeriodDays != null ? notAfter.plusSeconds(gracePeriodDays * 86_400L) : null;
    }

    /** 파일 이름에 쓸 수 없는 문자는 '_' 로. */
    public String downloadFileName() {
        return siteId.replaceAll("[^A-Za-z0-9_-]", "_") + "_" + licenseId + ".lic";
    }
}

package com.personaai.license.server.license;

import java.time.Instant;
import java.util.Map;
import java.util.UUID;

/**
 * 발급 장부 1행(license_ver 1). channels = 항목 → 세부 항목 → 상한(입력 순서).
 * devUntil = 개발(dev)의 총 개발기한(서버 기록 전용, V6 이전에 발급된 개발 라이선스는 null).
 */
public record LicenseRecord(
        long id, String licenseId, UUID submissionToken, Status status, String projectName, String licenseType,
        String siteId, Instant issuedAt, Instant notBefore, Instant notAfter, String warningNotice,
        int licenseVer, String solution, int channelsTotal,
        Map<String, Map<String, Integer>> channels,
        String licenseText, String licenseSha256, String publicKeyFp,
        String renewedFrom, long issuedBy, String issuedByName, String error, String voidReason, Instant voidedAt,
        Instant createdAt, Instant devUntil) {

    public enum Status { PENDING, ISSUED, FAILED, VOID }

    /** 운영 영구 표기(9999-12-31T23:59:59Z). */
    public boolean permanent() {
        return Contract.PERMANENT.equals(notAfter);
    }

    /** 개발(dev)의 총 개발기한 안에 다음 갱신 발급 기간이 남지 않음(종료 다음 날이 총 기한을 넘음): 연장 계약 확인 대상. */
    public boolean devTermEnding() {
        return devUntil != null && notAfter.plusSeconds(86_400).isAfter(devUntil);
    }

    /** 파일 이름에 쓸 수 없는 문자는 '_' 로. */
    public String downloadFileName() {
        return siteId.replaceAll("[^A-Za-z0-9_-]", "_") + "_" + licenseId + ".lic";
    }
}

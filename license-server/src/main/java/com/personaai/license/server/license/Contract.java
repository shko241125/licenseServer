package com.personaai.license.server.license;

import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.LinkedHashMap;
import java.util.Map;

import tools.jackson.databind.json.JsonMapper;

/** licensectl 에 넘길 계약 내용(서명 대상). 시각은 초 단위 UTC. */
public record Contract(String projectName, String licenseType, String siteId, int onlineStt, int offlineStt,
                       Instant issuedAt, Instant notBefore, Instant notAfter, int gracePeriodDays,
                       String warningNotice, String renewedFrom) {

    private static final DateTimeFormatter UTC = DateTimeFormatter.ofPattern("uuuu-MM-dd'T'HH:mm:ss'Z'").withZone(ZoneOffset.UTC);
    private static final JsonMapper JSON = JsonMapper.builder().build();

    public static String utc(Instant t) {
        return UTC.format(t);
    }

    /** 문자열 이어붙이기가 아니라 JSON 직렬화기로 만든다(따옴표·제어문자 이스케이프를 직접 하지 않음). */
    public byte[] toJson(String licenseId) {
        Map<String, Object> channels = new LinkedHashMap<>();
        channels.put("online_stt", onlineStt);
        channels.put("offline_stt", offlineStt);
        Map<String, Object> validity = new LinkedHashMap<>();
        validity.put("issued_at", utc(issuedAt));
        validity.put("not_before", utc(notBefore));
        validity.put("not_after", utc(notAfter));
        validity.put("grace_period_days", gracePeriodDays);
        Map<String, Object> root = new LinkedHashMap<>();
        root.put("format_version", 1);
        root.put("license_id", licenseId);
        root.put("project_name", projectName);
        root.put("license_type", licenseType);
        root.put("site_id", siteId);
        root.put("allowed_channels", channels);
        root.put("validity", validity);
        root.put("warning_notice", warningNotice);
        return JSON.writeValueAsBytes(root);
    }
}

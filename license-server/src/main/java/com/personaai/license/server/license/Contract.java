package com.personaai.license.server.license;

import java.time.Instant;
import java.time.ZoneOffset;
import java.time.format.DateTimeFormatter;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

import tools.jackson.databind.json.JsonMapper;

/**
 * licensectl 에 넘길 계약 내용(서명 대상, license_ver 1). 시각은 초 단위 UTC.
 * channels 는 allowed_channels 의 total 을 뺀 항목들: 항목 이름(자유, 예: offline) → 세부 항목 → 상한(입력 순서 유지).
 * total 은 모든 항목·세부 항목 합의 동시 세션 상한.
 * devUntil 은 개발(dev)의 총 개발기한으로 발급 서버에만 기록한다(서명 대상 아님, toJson 에 넣지 않음).
 */
public record Contract(String projectName, String licenseType, String solution, String siteId, int total,
                       Map<String, Map<String, Integer>> channels,
                       Instant issuedAt, Instant notBefore, Instant notAfter,
                       String warningNotice, String renewedFrom, Instant devUntil) {

    public static final int LICENSE_VER = 1;
    /** 운영(production) 라이선스의 영구 표기. core/src/detail.h kPermanentNotAfter 와 같은 값. */
    public static final Instant PERMANENT = Instant.parse("9999-12-31T23:59:59Z");
    /** 1회 발급 기간 상한(not_after - not_before). core/src/detail.h kTrialMaxSeconds·kDevMaxSeconds 와 같은 값. */
    public static final java.time.Duration TRIAL_MAX = java.time.Duration.ofDays(180);
    public static final java.time.Duration DEV_MAX = java.time.Duration.ofDays(366);

    private static final DateTimeFormatter UTC = DateTimeFormatter.ofPattern("uuuu-MM-dd'T'HH:mm:ss'Z'").withZone(ZoneOffset.UTC);
    private static final JsonMapper JSON = JsonMapper.builder().build();

    public Contract {
        // total 은 allowed_channels 의 예약 키: 항목 이름으로 오면 직렬화 때 total 값을 덮어쓴다.
        if (channels.containsKey("total")) throw new IllegalArgumentException("'total' is not a channel group name");
        Map<String, Map<String, Integer>> copy = new LinkedHashMap<>();  // 입력 순서 유지 + 변경 불가(깊은 복사)
        channels.forEach((g, items) -> copy.put(g, Collections.unmodifiableMap(new LinkedHashMap<>(items))));
        channels = Collections.unmodifiableMap(copy);
    }

    public static String utc(Instant t) {
        return UTC.format(t);
    }

    /** 문자열 이어붙이기가 아니라 JSON 직렬화기로 만든다(따옴표·제어문자 이스케이프를 직접 하지 않음). */
    public byte[] toJson(String licenseId) {
        Map<String, Object> allowed = new LinkedHashMap<>();
        allowed.put("total", total);
        allowed.putAll(channels);
        Map<String, Object> validity = new LinkedHashMap<>();
        validity.put("issued_at", utc(issuedAt));
        validity.put("not_before", utc(notBefore));
        validity.put("not_after", utc(notAfter));
        Map<String, Object> root = new LinkedHashMap<>();
        root.put("license_ver", LICENSE_VER);
        root.put("license_id", licenseId);
        root.put("project_name", projectName);
        root.put("license_type", licenseType);
        root.put("solution", solution);
        root.put("site_id", siteId);
        root.put("allowed_channels", allowed);
        root.put("validity", validity);
        root.put("warning_notice", warningNotice);
        return JSON.writeValueAsBytes(root);
    }

    /** DB channels_json 열: {"항목":{"세부 항목":상한,…},…} (total 제외, 입력 순서 유지, 화면 표시용). */
    public String channelsJson() {
        return JSON.writeValueAsString(channels);
    }

    public boolean permanent() {
        return PERMANENT.equals(notAfter);
    }
}

package com.personaai.license.server.license;

import java.time.Duration;
import java.time.Instant;
import java.time.LocalDate;
import java.time.LocalTime;
import java.time.ZoneId;
import java.time.format.DateTimeParseException;
import java.time.temporal.ChronoUnit;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * 입력 편의용 검증(즉시 피드백). 최종 판정은 licensectl(SignContract) 이 한다.
 * 규칙은 core/src/license.cpp ExtractFields·SignContract(license_ver 1)와 같게 유지하며, 경계값 테스트
 * (ValidationParityTest)로 일치를 확인한다. 서버가 더 엄격한 곳: 시험·개발의 9999년 종료일 거부,
 * (항목, 세부 항목) 중복 행, 이름 없이 상한만 있는 행, 개발의 총 개발기한(서버 기록 전용 — 코어는 모름).
 */
public final class ContractValidator {
    /** 발급 유형(순서 = 화면 선택 순서): 운영(영구) · 시험/데모 · 개발. */
    public static final List<String> TYPES = List.of("production", "trial", "dev");
    public static final int MAX_CHANNELS = 100_000;
    public static final int MAX_GROUPS = 64;   // allowed_channels 항목 수(total 제외)
    public static final int MAX_ENGINES = 64;  // 항목당 세부 항목 수
    static final Pattern NAME = Pattern.compile("[a-z0-9_]{1,64}");

    private ContractValidator() {}

    public record Result(Contract contract, Map<String, String> errors, List<String> warnings) {
        public boolean ok() {
            return errors.isEmpty();
        }
    }

    public static Result validate(IssueForm f, ZoneId zone, Instant now) {
        Map<String, String> e = new LinkedHashMap<>();
        List<String> w = new ArrayList<>();
        text(e, "projectName", f.getProjectName(), 256);
        text(e, "solution", f.getSolution(), 128);
        text(e, "siteId", f.getSiteId(), 256);
        text(e, "warningNotice", f.getWarningNotice(), 4096);
        String type = f.getLicenseType();
        if (type == null || !TYPES.contains(type)) e.put("licenseType", "production, trial, dev 중 하나를 고르세요.");

        Integer total = f.getTotal();
        if (total == null || total < 1 || total > MAX_CHANNELS) e.put("total", "1~" + MAX_CHANNELS + " 사이의 정수를 입력하세요.");
        int cap = e.containsKey("total") ? MAX_CHANNELS : total;
        Map<String, Map<String, Integer>> channels = channels(e, f.getChannels(), cap);

        Instant nb = instant(e, "notBefore", f.getNotBeforeDate(), f.getNotBeforeTime(), zone);
        Instant na;
        if ("production".equals(type)) {
            na = Contract.PERMANENT;  // 운영은 영구: 종료 입력은 쓰지 않는다
        } else {
            na = instant(e, "notAfter", f.getNotAfterDate(), f.getNotAfterTime(), zone);
            if (na != null && !na.isBefore(Instant.parse("9999-01-01T00:00:00Z"))) {
                e.put("notAfter", "시험·개발은 실제 종료일을 입력하세요(영구 발급은 운영만).");
            }
        }
        if (nb != null && na != null && !e.containsKey("notAfter") && !nb.isBefore(na)) e.put("notAfter", "종료는 시작보다 뒤여야 합니다.");
        // 1회 발급 기간 상한(코어 SignContract 와 같음): 시험 180일, 개발 366일(1년, 윤년 포함)
        if (nb != null && na != null && !e.containsKey("notAfter")) {
            if ("trial".equals(type) && Duration.between(nb, na).compareTo(Contract.TRIAL_MAX) > 0) {
                e.put("notAfter", "시험(trial)은 1회 180일 이내로 발급합니다(종료 − 시작). 더 필요하면 갱신 발급하세요.");
            } else if ("dev".equals(type) && Duration.between(nb, na).compareTo(Contract.DEV_MAX) > 0) {
                e.put("notAfter", "개발(dev)은 1회 1년(366일) 이내로 발급합니다. 총 개발기한 안에서 갱신 발급하세요.");
            }
        }
        // 개발: 총 개발기한(그날 23:59:59 KST) 필수, 발급 종료는 그 안이어야 한다. 다른 유형은 입력을 쓰지 않는다.
        Instant devUntil = null;
        if ("dev".equals(type)) {
            if (f.getDevUntilDate() == null || f.getDevUntilDate().isBlank()) {
                e.put("devUntil", "개발(dev)은 총 개발기한이 필수입니다.");
            } else {
                devUntil = instant(e, "devUntil", f.getDevUntilDate(), "23:59:59", zone);
                if (devUntil != null && na != null && !e.containsKey("notAfter") && na.isAfter(devUntil)) {
                    e.put("devUntil", "발급 종료가 총 개발기한을 넘습니다. 과제 기한이 연장됐다면 총 개발기한을 고치세요.");
                }
            }
        }
        if (f.getRenewedFrom() != null && !f.getRenewedFrom().isEmpty() && !f.getRenewedFrom().matches("[0-9a-f]{32}")) {
            e.put("renewedFrom", "잘못된 원본 라이선스 ID");
        }
        if (!e.isEmpty()) return new Result(null, e, w);

        if (nb.isBefore(now.minusSeconds(86_400))) w.add("시작 시각이 하루 이상 과거입니다.");
        if (!"production".equals(type)) {
            if (na.isBefore(now)) w.add("종료 시각이 이미 지났습니다. 발급해도 즉시 만료 상태입니다.");
        }
        int sum = channels.values().stream().flatMap(m -> m.values().stream()).mapToInt(Integer::intValue).sum();
        if (sum < total) w.add("세부 항목 상한의 합(" + sum + ")이 total(" + total + ")보다 작아 total 까지 쓸 수 없습니다.");
        Contract c = new Contract(f.getProjectName().strip(), type, f.getSolution().strip(), f.getSiteId().strip(),
                total, channels, now.truncatedTo(ChronoUnit.SECONDS), nb, na, f.getWarningNotice().strip(),
                f.getRenewedFrom() == null || f.getRenewedFrom().isEmpty() ? null : f.getRenewedFrom(), devUntil);
        return new Result(c, e, w);
    }

    /**
     * 채널 행 → 항목 → 세부 항목 → 상한(입력 순서). 세 칸이 모두 빈 행·상한 0 인 행은 제외.
     * 오류는 첫 번째만 "channels" 에 기록한다.
     */
    private static Map<String, Map<String, Integer>> channels(Map<String, String> e, List<IssueForm.ChannelRow> rows, int total) {
        Map<String, Map<String, Integer>> m = new LinkedHashMap<>();
        Set<String> seen = new HashSet<>();
        for (IssueForm.ChannelRow r : rows == null ? List.<IssueForm.ChannelRow>of() : rows) {
            if (r == null) continue;  // 요청 바인딩에서 빈 자리가 생길 수 있다
            String g = r.getGroup() == null ? "" : r.getGroup().strip();
            String name = r.getName() == null ? "" : r.getName().strip();
            Integer n = r.getCount();
            boolean counted = n != null && n != 0;
            if (g.isEmpty() || name.isEmpty()) {
                if (!counted) continue;  // 상한이 비었거나 0 인 행은 쓰지 않는다
                e.put("channels", "상한을 입력한 행에 항목 이름과 세부 항목 이름이 모두 있어야 합니다.");
                return m;
            }
            String label = g + "." + name;
            if (!NAME.matcher(g).matches()) { e.put("channels", "항목 이름 '" + g + "': 소문자·숫자·_ 1~64자만 쓸 수 있습니다."); return m; }
            if ("total".equals(g)) { e.put("channels", "'total' 은 전체 상한이라 항목 이름으로 쓸 수 없습니다."); return m; }
            if (!NAME.matcher(name).matches()) { e.put("channels", "세부 항목 이름 '" + name + "': 소문자·숫자·_ 1~64자만 쓸 수 있습니다."); return m; }
            if (n == null || n < 0 || n > MAX_CHANNELS) { e.put("channels", "'" + label + "': 0~" + MAX_CHANNELS + " 사이의 정수를 입력하세요."); return m; }
            if (n > total) { e.put("channels", "'" + label + "': total(" + total + ")보다 클 수 없습니다."); return m; }
            if (!seen.add(label)) { e.put("channels", "'" + label + "' 이 중복되었습니다."); return m; }
            if (n > 0) m.computeIfAbsent(g, k -> new LinkedHashMap<>()).put(name, n);
        }
        if (m.size() > MAX_GROUPS) { e.put("channels", "항목은 " + MAX_GROUPS + "개 이하여야 합니다."); return m; }
        for (var entry : m.entrySet()) {
            if (entry.getValue().size() > MAX_ENGINES) {
                e.put("channels", "항목 '" + entry.getKey() + "' 의 세부 항목은 " + MAX_ENGINES + "개 이하여야 합니다.");
                return m;
            }
        }
        if (m.isEmpty()) e.put("channels", "세부 항목을 하나 이상 1 이상으로 입력하세요.");
        return m;
    }

    /** 길이는 코어와 같게 UTF-8 바이트로 센다(한글 1자 = 3바이트). */
    private static void text(Map<String, String> e, String field, String v, int max) {
        if (v == null || v.isBlank()) {
            e.put(field, "필수 입력입니다.");
        } else if (v.strip().getBytes(StandardCharsets.UTF_8).length > max) {
            e.put(field, max + "바이트 이하로 입력하세요(영문 " + max + "자, 한글 약 " + max / 3 + "자).");
        } else if (v.strip().chars().anyMatch(c -> c < 0x20 || c == 0x7F)) {
            e.put(field, "줄바꿈·탭 등 제어문자는 쓸 수 없습니다.");
        }
    }

    private static Instant instant(Map<String, String> e, String field, String date, String time, ZoneId zone) {
        try {
            LocalDate d = LocalDate.parse(date == null ? "" : date);
            String t = time == null || time.isBlank() ? "00:00:00" : time;
            if (t.length() == 5) t = t + ":00";
            Instant i = d.atTime(LocalTime.parse(t)).atZone(zone).toInstant();
            if (i.isBefore(Instant.EPOCH) || d.getYear() > 9999) throw new DateTimeParseException("range", t, 0);
            return i;
        } catch (DateTimeParseException ex) {
            e.put(field, "날짜(YYYY-MM-DD)와 시각(HH:MM[:SS])을 확인하세요.");
            return null;
        }
    }
}

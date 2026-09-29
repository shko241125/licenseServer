package com.personaai.license.server.license;

import java.time.Instant;
import java.time.LocalDate;
import java.time.LocalTime;
import java.time.ZoneId;
import java.time.format.DateTimeParseException;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * 입력 편의용 검증(즉시 피드백). 최종 판정은 licensectl(SignContract) 이 한다.
 * 규칙은 core/src/license.cpp ExtractFields 와 같게 유지하며 경계값 테스트로 일치를 확인한다.
 */
public final class ContractValidator {
    public static final Set<String> TYPES = Set.of("production", "trial", "poc");
    public static final int MAX_CHANNELS = 100_000;
    public static final int MAX_GRACE = 90;

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
        text(e, "siteId", f.getSiteId(), 256);
        text(e, "warningNotice", f.getWarningNotice(), 4096);
        if (f.getLicenseType() == null || !TYPES.contains(f.getLicenseType())) e.put("licenseType", "production, trial, poc 중 하나를 고르세요.");
        range(e, "onlineStt", f.getOnlineStt(), 0, MAX_CHANNELS);
        range(e, "offlineStt", f.getOfflineStt(), 0, MAX_CHANNELS);
        range(e, "gracePeriodDays", f.getGracePeriodDays(), 0, MAX_GRACE);
        if (!e.containsKey("onlineStt") && !e.containsKey("offlineStt") && f.getOnlineStt() + f.getOfflineStt() == 0) {
            e.put("offlineStt", "온라인·오프라인 채널 중 하나는 1 이상이어야 합니다.");
        }
        Instant nb = instant(e, "notBefore", f.getNotBeforeDate(), f.getNotBeforeTime(), zone);
        Instant na = instant(e, "notAfter", f.getNotAfterDate(), f.getNotAfterTime(), zone);
        if (nb != null && na != null && !nb.isBefore(na)) e.put("notAfter", "종료는 시작보다 뒤여야 합니다.");
        if (f.getRenewedFrom() != null && !f.getRenewedFrom().isEmpty() && !f.getRenewedFrom().matches("[0-9a-f]{32}")) {
            e.put("renewedFrom", "잘못된 원본 라이선스 ID");
        }
        if (!e.isEmpty()) return new Result(null, e, w);

        if (nb.isBefore(now.minusSeconds(86_400))) w.add("시작 시각이 하루 이상 과거입니다.");
        if (na.isAfter(nb.plusSeconds(400L * 86_400))) w.add("유효 기간이 1년을 넘습니다.");
        if (na.isBefore(now)) w.add("종료 시각이 이미 지났습니다. 발급해도 즉시 만료(또는 유예) 상태입니다.");
        Contract c = new Contract(f.getProjectName().strip(), f.getLicenseType(), f.getSiteId().strip(),
                f.getOnlineStt(), f.getOfflineStt(), now.truncatedTo(java.time.temporal.ChronoUnit.SECONDS), nb, na,
                f.getGracePeriodDays(), f.getWarningNotice().strip(),
                f.getRenewedFrom() == null || f.getRenewedFrom().isEmpty() ? null : f.getRenewedFrom());
        return new Result(c, e, w);
    }

    private static void text(Map<String, String> e, String field, String v, int max) {
        if (v == null || v.isBlank()) {
            e.put(field, "필수 입력입니다.");
        } else if (v.strip().length() > max) {
            e.put(field, max + "자 이하로 입력하세요.");
        } else if (v.strip().chars().anyMatch(c -> c < 0x20 || c == 0x7F)) {
            e.put(field, "줄바꿈·탭 등 제어문자는 쓸 수 없습니다.");
        }
    }

    private static void range(Map<String, String> e, String field, Integer v, int lo, int hi) {
        if (v == null || v < lo || v > hi) e.put(field, lo + "~" + hi + " 사이의 정수를 입력하세요.");
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

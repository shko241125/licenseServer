package com.personaai.license.server.license;

import java.time.LocalDate;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;

/**
 * 발급 입력 폼(license_ver 1). 날짜·시각은 KST(설정 zone) 문자열로 받아 서버에서 해석한다
 * (브라우저가 초 단위를 생략해 "HH:mm" 으로 보낼 수 있어 직접 파싱).
 * 채널은 (항목, 세부 항목, 상한) 행 목록 하나로 받는다(JS 없이 고정 행). 항목 이름은 자유(예: offline, online).
 * 세 칸이 모두 빈 행과 상한 0 인 행은 계약에 넣지 않는다.
 * 행 목록의 기본값은 비어 있다: 요청에 온 행만 쓰고, 기본 행은 새 폼을 보여 줄 때만 채운다(withDefaultEngines).
 * (필드 기본값에 행을 두면 요청에 없는 행이 그대로 남아 계약에 섞인다.)
 * 운영(production)은 종료 입력과 관계없이 영구(Contract.PERMANENT)로 발급한다.
 */
public class IssueForm {
    /** 새 폼에 기본으로 보여 줄 행(요구 예시). 항목·세부 항목 이름은 열린 목록이라 다른 이름도 입력할 수 있다. */
    static final Map<String, Map<String, Integer>> DEFAULT_CHANNELS = defaults();
    static final int MIN_ROWS = 10;
    static final int SPARE_ROWS = 3;
    public static final String DEFAULT_SOLUTION = "sonastt-universal";

    /** 채널 입력 행 1개: allowed_channels.<group>.<name> = count. */
    public static class ChannelRow {
        private String group;
        private String name;
        private Integer count;

        public ChannelRow() {}

        public ChannelRow(String group, String name, Integer count) {
            this.group = group;
            this.name = name;
            this.count = count;
        }

        public String getGroup() { return group; }
        public void setGroup(String v) { group = v; }
        public String getName() { return name; }
        public void setName(String v) { name = v; }
        public Integer getCount() { return count; }
        public void setCount(Integer v) { count = v; }
    }

    private String projectName;
    private String licenseType = "production";
    private String solution = DEFAULT_SOLUTION;
    private String siteId;
    private Integer total = 0;
    private List<ChannelRow> channels = new ArrayList<>();
    private String notBeforeDate;
    private String notBeforeTime = "00:00:00";
    private String notAfterDate;
    private String notAfterTime = "23:59:59";
    private String warningNotice;
    private UUID submissionToken = UUID.randomUUID();
    private String renewedFrom;

    /** 항목 → 세부 항목 → 상한으로 행을 만들고 빈 행을 붙인다(padChannels). */
    static List<ChannelRow> rows(Map<String, Map<String, Integer>> groups) {
        List<ChannelRow> r = new ArrayList<>();
        groups.forEach((g, items) -> items.forEach((n, c) -> r.add(new ChannelRow(g, n, c))));
        return pad(r);
    }

    private static boolean blank(ChannelRow r) {
        return r == null || ((r.group == null || r.group.isBlank()) && (r.name == null || r.name.isBlank())
                && (r.count == null || r.count == 0));
    }

    /** 끝에 빈 행이 SPARE_ROWS 개, 전체가 MIN_ROWS 행 이상이 되도록 빈 행을 덧붙인다. 기존 행의 순서·번호는 그대로. */
    private static List<ChannelRow> pad(List<ChannelRow> r) {
        int trailing = 0;
        for (int i = r.size() - 1; i >= 0 && blank(r.get(i)); i--) trailing++;
        while (trailing < SPARE_ROWS || r.size() < MIN_ROWS) {
            r.add(new ChannelRow("", "", 0));
            trailing++;
        }
        return r;
    }

    /** 폼을 다시 보여 줄 때(입력 오류, 미리보기에서 "수정") 빈 행을 보충한다. 행이 모자라 입력을 못 하는 일 방지. */
    public IssueForm padChannels() {
        List<ChannelRow> r = new ArrayList<>();
        if (channels != null) for (ChannelRow c : channels) r.add(c == null ? new ChannelRow("", "", 0) : c);
        channels = pad(r);
        return this;
    }

    private static Map<String, Map<String, Integer>> defaults() {
        Map<String, Map<String, Integer>> m = new LinkedHashMap<>();
        for (String row : List.of("offline.sonastt", "offline.sonaml", "offline.sonaasr", "online.sonastt", "online.sonaasr")) {
            String[] p = row.split("\\.");
            m.computeIfAbsent(p[0], k -> new LinkedHashMap<>()).put(p[1], 0);
        }
        return m;
    }

    /** 새 발급 폼: 요구 예시의 항목·세부 항목 이름으로 행을 채운다(상한 0). */
    public IssueForm withDefaultEngines() {
        channels = rows(DEFAULT_CHANNELS);
        return this;
    }

    /**
     * 갱신 발급: 원본 계약 내용을 채운다.
     * 기간 — 시험·PoC 는 원본 종료 다음 날부터 1년, 운영(영구) 원본은 오늘부터.
     * 이전 형식(format_version) 원본은 채널 값을 옮길 수 없어(형식이 다름) 기본 행만 둔다.
     */
    public static IssueForm copyOf(LicenseRecord r, ZoneId zone, LocalDate today) {
        IssueForm f = new IssueForm();
        f.projectName = r.projectName();
        f.licenseType = r.licenseType();
        f.siteId = r.siteId();
        f.warningNotice = r.warningNotice();
        f.renewedFrom = r.licenseId();
        if (r.legacy()) {
            f.withDefaultEngines();
        } else {
            f.solution = r.solution();
            f.total = r.channelsTotal();
            f.channels = rows(r.channels());
        }
        LocalDate start = r.permanent() ? today : r.notAfter().atZone(zone).toLocalDate().plusDays(1);
        f.notBeforeDate = start.toString();
        f.notAfterDate = start.plusYears(1).minusDays(1).toString();
        return f;
    }

    public String getProjectName() { return projectName; }
    public void setProjectName(String v) { projectName = v; }
    public String getLicenseType() { return licenseType; }
    public void setLicenseType(String v) { licenseType = v; }
    public String getSolution() { return solution; }
    public void setSolution(String v) { solution = v; }
    public String getSiteId() { return siteId; }
    public void setSiteId(String v) { siteId = v; }
    public Integer getTotal() { return total; }
    public void setTotal(Integer v) { total = v; }
    public List<ChannelRow> getChannels() { return channels; }
    public void setChannels(List<ChannelRow> v) { channels = v; }
    public String getNotBeforeDate() { return notBeforeDate; }
    public void setNotBeforeDate(String v) { notBeforeDate = v; }
    public String getNotBeforeTime() { return notBeforeTime; }
    public void setNotBeforeTime(String v) { notBeforeTime = v; }
    public String getNotAfterDate() { return notAfterDate; }
    public void setNotAfterDate(String v) { notAfterDate = v; }
    public String getNotAfterTime() { return notAfterTime; }
    public void setNotAfterTime(String v) { notAfterTime = v; }
    public String getWarningNotice() { return warningNotice; }
    public void setWarningNotice(String v) { warningNotice = v; }
    public UUID getSubmissionToken() { return submissionToken; }
    public void setSubmissionToken(UUID v) { submissionToken = v; }
    public String getRenewedFrom() { return renewedFrom; }
    public void setRenewedFrom(String v) { renewedFrom = v; }
}

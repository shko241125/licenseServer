package com.personaai.license.server.license;

import java.util.UUID;

/**
 * 발급 입력 폼. 날짜·시각은 KST(설정 zone) 문자열로 받아 서버에서 해석한다
 * (브라우저가 초 단위를 생략해 "HH:mm" 으로 보낼 수 있어 직접 파싱).
 */
public class IssueForm {
    private String projectName;
    private String licenseType = "production";
    private String siteId;
    private Integer onlineStt = 0;
    private Integer offlineStt = 0;
    private String notBeforeDate;
    private String notBeforeTime = "00:00:00";
    private String notAfterDate;
    private String notAfterTime = "23:59:59";
    private Integer gracePeriodDays = 14;
    private String warningNotice;
    private UUID submissionToken = UUID.randomUUID();
    private String renewedFrom;

    public static IssueForm copyOf(LicenseRecord r, java.time.ZoneId zone) {
        IssueForm f = new IssueForm();
        f.projectName = r.projectName();
        f.licenseType = r.licenseType();
        f.siteId = r.siteId();
        f.onlineStt = r.onlineStt();
        f.offlineStt = r.offlineStt();
        f.gracePeriodDays = r.gracePeriodDays();
        f.warningNotice = r.warningNotice();
        f.renewedFrom = r.licenseId();
        // 갱신: 이전 종료 다음 날 00:00 부터 1년
        java.time.LocalDate start = r.notAfter().atZone(zone).toLocalDate().plusDays(1);
        f.notBeforeDate = start.toString();
        f.notAfterDate = start.plusYears(1).minusDays(1).toString();
        return f;
    }

    public String getProjectName() { return projectName; }
    public void setProjectName(String v) { projectName = v; }
    public String getLicenseType() { return licenseType; }
    public void setLicenseType(String v) { licenseType = v; }
    public String getSiteId() { return siteId; }
    public void setSiteId(String v) { siteId = v; }
    public Integer getOnlineStt() { return onlineStt; }
    public void setOnlineStt(Integer v) { onlineStt = v; }
    public Integer getOfflineStt() { return offlineStt; }
    public void setOfflineStt(Integer v) { offlineStt = v; }
    public String getNotBeforeDate() { return notBeforeDate; }
    public void setNotBeforeDate(String v) { notBeforeDate = v; }
    public String getNotBeforeTime() { return notBeforeTime; }
    public void setNotBeforeTime(String v) { notBeforeTime = v; }
    public String getNotAfterDate() { return notAfterDate; }
    public void setNotAfterDate(String v) { notAfterDate = v; }
    public String getNotAfterTime() { return notAfterTime; }
    public void setNotAfterTime(String v) { notAfterTime = v; }
    public Integer getGracePeriodDays() { return gracePeriodDays; }
    public void setGracePeriodDays(Integer v) { gracePeriodDays = v; }
    public String getWarningNotice() { return warningNotice; }
    public void setWarningNotice(String v) { warningNotice = v; }
    public UUID getSubmissionToken() { return submissionToken; }
    public void setSubmissionToken(UUID v) { submissionToken = v; }
    public String getRenewedFrom() { return renewedFrom; }
    public void setRenewedFrom(String v) { renewedFrom = v; }
}

package com.personaai.license.server.license;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;
import java.time.ZoneId;

import org.junit.jupiter.api.Test;

class ContractValidatorTest {
    static final ZoneId KST = ZoneId.of("Asia/Seoul");
    static final Instant NOW = Instant.parse("2026-09-29T00:00:00Z");

    static IssueForm valid() {
        IssueForm f = new IssueForm();
        f.setProjectName("STT Platform");
        f.setSiteId("SEOUL-DC");
        f.setOfflineStt(8);
        f.setNotBeforeDate("2026-10-01");
        f.setNotAfterDate("2027-09-30");
        f.setWarningNotice("notice");
        return f;
    }

    @Test
    void kstInputBecomesUtc() {
        var r = ContractValidator.validate(valid(), KST, NOW);
        assertThat(r.errors()).isEmpty();
        assertThat(Contract.utc(r.contract().notBefore())).isEqualTo("2026-09-30T15:00:00Z"); // KST 자정 = 전날 15시 UTC
        assertThat(Contract.utc(r.contract().notAfter())).isEqualTo("2027-09-30T14:59:59Z");
        assertThat(r.contract().issuedAt()).isEqualTo(NOW);
    }

    @Test
    void acceptsMinutePrecisionTime() {
        IssueForm f = valid();
        f.setNotBeforeTime("09:30");
        assertThat(Contract.utc(ContractValidator.validate(f, KST, NOW).contract().notBefore())).isEqualTo("2026-10-01T00:30:00Z");
    }

    @Test
    void errors() {
        IssueForm f = valid();
        f.setOnlineStt(0);
        f.setOfflineStt(0);
        f.setGracePeriodDays(91);
        f.setLicenseType("enterprise");
        f.setProjectName("a\tb");
        f.setSiteId(" ");
        f.setNotAfterDate("2026-10-01");
        f.setNotAfterTime("00:00:00");
        var r = ContractValidator.validate(f, KST, NOW);
        assertThat(r.errors()).containsKeys("offlineStt", "gracePeriodDays", "licenseType", "projectName", "siteId", "notAfter");
        f = valid();
        f.setNotBeforeDate("2026-02-30");
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("notBefore");
        f = valid();
        f.setRenewedFrom("xyz");
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("renewedFrom");
    }

    @Test
    void warnings() {
        IssueForm f = valid();
        f.setNotBeforeDate("2026-01-01");
        f.setNotAfterDate("2026-06-30");
        var r = ContractValidator.validate(f, KST, NOW);
        assertThat(r.errors()).isEmpty();
        assertThat(r.warnings()).hasSize(2); // 과거 시작, 이미 종료
    }

    @Test
    void contractJsonIsEscapedBySerializer() {
        IssueForm f = valid();
        f.setProjectName("Quote \" and \\ back 한글");
        String json = new String(ContractValidator.validate(f, KST, NOW).contract().toJson("0".repeat(32)),
                java.nio.charset.StandardCharsets.UTF_8);
        assertThat(json).contains("\"project_name\":\"Quote \\\" and \\\\ back 한글\"");
        assertThat(json).startsWith("{\"format_version\":1,\"license_id\":\"");
    }
}

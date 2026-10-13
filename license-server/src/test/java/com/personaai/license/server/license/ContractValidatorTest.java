package com.personaai.license.server.license;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.List;

import org.junit.jupiter.api.Test;

class ContractValidatorTest {
    static final ZoneId KST = ZoneId.of("Asia/Seoul");
    static final Instant NOW = Instant.parse("2026-09-29T00:00:00Z");

    /**
     * 요구 예시와 같은 운영 계약(영구): total 16, offline sonastt 8 / sonaml 4 / sonaasr 4, online sonastt 8 / sonaasr 2.
     * 기본 행 순서: 0~2 offline(sonastt, sonaml, sonaasr), 3~4 online(sonastt, sonaasr), 5~9 빈 행.
     */
    static IssueForm valid() {
        IssueForm f = new IssueForm().withDefaultEngines();
        f.setProjectName("STT Platform");
        f.setSiteId("SEOUL-DC");
        f.setTotal(16);
        int[] counts = {8, 4, 4, 8, 2};
        for (int i = 0; i < counts.length; i++) f.getChannels().get(i).setCount(counts[i]);
        f.setNotBeforeDate("2026-10-01");
        f.setNotAfterDate("2027-09-30");
        f.setWarningNotice("notice");
        return f;
    }

    static IssueForm trial() {
        IssueForm f = valid();
        f.setLicenseType("trial");
        return f;
    }

    @Test
    void productionIsPermanentAndChannelsFollowInputOrder() {
        var r = ContractValidator.validate(valid(), KST, NOW);
        assertThat(r.errors()).isEmpty();
        Contract c = r.contract();
        assertThat(Contract.utc(c.notBefore())).isEqualTo("2026-09-30T15:00:00Z"); // KST 자정 = 전날 15시 UTC
        assertThat(c.notAfter()).isEqualTo(Contract.PERMANENT);                    // 종료 입력(2027-09-30)은 무시
        assertThat(c.permanent()).isTrue();
        assertThat(c.issuedAt()).isEqualTo(NOW);
        assertThat(c.solution()).isEqualTo("sonastt-universal");
        assertThat(c.total()).isEqualTo(16);
        assertThat(c.channels()).containsOnlyKeys("offline", "online");
        assertThat(c.channels().get("offline")).containsExactly(
                java.util.Map.entry("sonastt", 8), java.util.Map.entry("sonaml", 4), java.util.Map.entry("sonaasr", 4));
        assertThat(c.channels().get("online")).containsExactly(java.util.Map.entry("sonastt", 8), java.util.Map.entry("sonaasr", 2));
        assertThat(c.channelsJson()).isEqualTo("{\"offline\":{\"sonastt\":8,\"sonaml\":4,\"sonaasr\":4},\"online\":{\"sonastt\":8,\"sonaasr\":2}}");
    }

    @Test
    void trialUsesEnteredEnd() {
        var r = ContractValidator.validate(trial(), KST, NOW);
        assertThat(r.errors()).isEmpty();
        assertThat(Contract.utc(r.contract().notAfter())).isEqualTo("2027-09-30T14:59:59Z");
        assertThat(r.contract().permanent()).isFalse();
    }

    @Test
    void licenseTypesAreProductionTrialDev() {
        assertThat(ContractValidator.TYPES).containsExactly("production", "trial", "dev");   // 화면 순서
        IssueForm f = trial();
        f.setLicenseType("dev");
        var r = ContractValidator.validate(f, KST, NOW);
        assertThat(r.errors()).isEmpty();
        assertThat(r.contract().permanent()).isFalse();
        f.setNotAfterDate("9999-12-31");
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("notAfter");     // 개발도 영구 불가
        for (String t : new String[] {"enterprise", "demo", "development", "Production", "", null}) {
            f = valid();
            f.setLicenseType(t);
            assertThat(ContractValidator.validate(f, KST, NOW).errors()).as(String.valueOf(t)).containsKey("licenseType");
        }
    }

    @Test
    void longTrialAndDevPeriodsAreNormal() {
        // 정부과제 등 2~5년 단위 시험·개발 라이선스: 오류도, 기간 경고도 없다(상한은 9999년 이후 종료 거부뿐)
        for (String type : new String[] {"trial", "dev"}) {
            IssueForm f = trial();
            f.setLicenseType(type);
            f.setNotBeforeDate("2026-10-01");
            f.setNotAfterDate("2031-09-30");      // 5년
            var r = ContractValidator.validate(f, KST, NOW);
            assertThat(r.errors()).as(type).isEmpty();
            assertThat(r.warnings()).as(type).isEmpty();
        }
    }

    @Test
    void renewalKeepsLicenseType() {
        Contract c = ContractValidator.validate(trial(), KST, NOW).contract();
        for (String type : ContractValidator.TYPES) {
            LicenseRecord rec = new LicenseRecord(1, "0".repeat(32), null, LicenseRecord.Status.ISSUED, c.projectName(),
                    type, c.siteId(), c.issuedAt(), c.notBefore(), c.notAfter(), c.warningNotice(), 1, c.solution(),
                    c.total(), c.channels(), null, null, null, "text", "sha", "fp", null, 1, "admin", null, null, null, NOW);
            assertThat(IssueForm.copyOf(rec, KST, java.time.LocalDate.of(2026, 10, 13)).getLicenseType()).isEqualTo(type);
        }
    }

    @Test
    void acceptsMinutePrecisionTime() {
        IssueForm f = valid();
        f.setNotBeforeTime("09:30");
        assertThat(Contract.utc(ContractValidator.validate(f, KST, NOW).contract().notBefore())).isEqualTo("2026-10-01T00:30:00Z");
    }

    @Test
    void zeroAndBlankRowsAreSkipped() {
        IssueForm f = valid();
        f.getChannels().get(1).setCount(0);        // offline.sonaml 0 → 제외
        f.getChannels().get(5).setName(" ");       // 빈 행 + 0 → 무시
        f.getChannels().get(6).setGroup("gpu");    // 항목만 있고 상한 0 → 무시
        f.getChannels().get(3).setCount(0);
        f.getChannels().get(4).setCount(0);        // online 이 모두 0 → 항목 자체가 빠짐
        var r = ContractValidator.validate(f, KST, NOW);
        assertThat(r.errors()).isEmpty();
        assertThat(r.contract().channels()).containsOnlyKeys("offline");
        assertThat(r.contract().channels().get("offline")).containsOnlyKeys("sonastt", "sonaasr");
    }

    @Test
    void customGroupsKeepInputOrderAndSameItemNamesAreIndependent() {
        IssueForm f = valid();
        f.setChannels(new ArrayList<>(List.of(
                new IssueForm.ChannelRow("streaming", "sonastt", 3),
                new IssueForm.ChannelRow("batch_gpu", "sonastt", 4),
                new IssueForm.ChannelRow("streaming", "sonaasr", 1))));   // 같은 항목이 떨어져 있어도 한 항목으로
        var r = ContractValidator.validate(f, KST, NOW);
        assertThat(r.errors()).isEmpty();
        assertThat(r.contract().channels().keySet()).containsExactly("streaming", "batch_gpu");
        assertThat(r.contract().channels().get("streaming")).containsExactly(
                java.util.Map.entry("sonastt", 3), java.util.Map.entry("sonaasr", 1));
        assertThat(r.contract().channelsJson())
                .isEqualTo("{\"streaming\":{\"sonastt\":3,\"sonaasr\":1},\"batch_gpu\":{\"sonastt\":4}}");
        String json = new String(r.contract().toJson("0".repeat(32)), java.nio.charset.StandardCharsets.UTF_8);
        assertThat(json).contains("\"allowed_channels\":{\"total\":16,\"streaming\":{\"sonastt\":3,\"sonaasr\":1},"
                + "\"batch_gpu\":{\"sonastt\":4}}");
    }

    @Test
    void contractRejectsTotalAsGroupName() {
        assertThatThrownBy(() -> new Contract("p", "trial", "s", "site", 1,
                java.util.Map.of("total", java.util.Map.of("a", 1)), NOW, NOW, NOW.plusSeconds(1), "n", null))
                .isInstanceOf(IllegalArgumentException.class);
    }

    @Test
    void errors() {
        IssueForm f = valid();
        f.setLicenseType("enterprise");
        f.setProjectName("a\tb");
        f.setSiteId(" ");
        f.setSolution("");
        f.setTotal(0);
        var r = ContractValidator.validate(f, KST, NOW);
        assertThat(r.errors()).containsKeys("licenseType", "projectName", "siteId", "solution", "total");

        record Bad(String why, String group, String name, Integer count) {}
        for (Bad b : List.of(
                new Bad("> total", "offline", "sonastt", 17),
                new Bad("세부 항목 이름 규칙", "online", "Sona-ASR", 1),
                new Bad("항목 이름 규칙", "On-Line", "sonastt", 1),
                new Bad("total 은 예약", "total", "sonastt", 1),
                new Bad("세부 항목 이름 없이 상한", "online", "", 3),
                new Bad("항목 이름 없이 상한", "", "sonastt", 3),
                new Bad("둘 다 없이 상한", "", "", 3),
                new Bad("중복", "offline", "sonastt", 1),
                new Bad("상한 빈 칸", "offline", "sonanew", null),
                new Bad("음수", "offline", "sonanew", -1))) {
            IssueForm f2 = valid();
            f2.getChannels().set(5, new IssueForm.ChannelRow(b.group(), b.name(), b.count()));
            assertThat(ContractValidator.validate(f2, KST, NOW).errors()).as(b.why()).containsKey("channels");
        }
        f = valid();
        for (var row : f.getChannels()) row.setCount(0);
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("channels");  // 모두 0
        f = valid();
        f.setChannels(null);
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("channels");

        f = trial();
        f.setNotAfterDate("2026-10-01");
        f.setNotAfterTime("00:00:00");
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("notAfter");  // 시작 = 종료
        f = trial();
        f.setNotAfterDate("9999-12-31");
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("notAfter");  // 시험은 영구 불가
        f = valid();
        f.setNotBeforeDate("2026-02-30");
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("notBefore");
        f = valid();
        f.setRenewedFrom("xyz");
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("renewedFrom");
    }

    @Test
    void textLimitsAreUtf8BytesLikeCore() {
        IssueForm f = valid();
        f.setProjectName("가".repeat(85));   // 255 바이트
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).doesNotContainKey("projectName");
        f.setProjectName("가".repeat(86));   // 258 바이트 > 256
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("projectName");
    }

    @Test
    void itemAndGroupCountLimits() {
        IssueForm f = valid();
        List<IssueForm.ChannelRow> rows = new ArrayList<>();
        for (int i = 0; i < 65; i++) rows.add(new IssueForm.ChannelRow("offline", "e" + i, 1));
        f.setChannels(rows);
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("channels");   // 항목당 65
        f.setChannels(new ArrayList<>(rows.subList(0, 64)));
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).isEmpty();
        rows = new ArrayList<>();
        for (int i = 0; i < 65; i++) rows.add(new IssueForm.ChannelRow("g" + i, "sonastt", 1));
        f.setChannels(rows);
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("channels");   // 항목 65
        f.setChannels(new ArrayList<>(rows.subList(0, 64)));
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).isEmpty();
    }

    @Test
    void padChannelsAppendsSpareRowsWithoutMovingExisting() {
        IssueForm f = valid();
        List<IssueForm.ChannelRow> rows = new ArrayList<>();
        for (int i = 0; i < 12; i++) rows.add(new IssueForm.ChannelRow("g", "e" + i, 1));
        rows.add(null);                                       // 바인딩 빈 자리
        f.setChannels(rows);
        f.padChannels();
        assertThat(f.getChannels()).hasSize(15);              // 12 + (null → 빈 행) + 2 → 끝 빈 행 3
        assertThat(f.getChannels().get(11).getName()).isEqualTo("e11");
        assertThat(f.getChannels()).doesNotContainNull();
        f.padChannels();
        assertThat(f.getChannels()).hasSize(15);              // 이미 빈 행 3개면 그대로
        f.setChannels(null);
        assertThat(f.padChannels().getChannels()).hasSize(IssueForm.MIN_ROWS);
        f.getChannels().set(3, null);
        assertThat(ContractValidator.validate(f, KST, NOW).errors()).containsKey("channels");   // null 행 무시, 모두 빈 행
    }

    @Test
    void renewalCopiesCustomGroups() {
        IssueForm f = valid();
        f.getChannels().set(5, new IssueForm.ChannelRow("streaming", "sonastt", 2));
        Contract c = ContractValidator.validate(f, KST, NOW).contract();
        LicenseRecord rec = new LicenseRecord(1, "0".repeat(32), null, LicenseRecord.Status.ISSUED, c.projectName(),
                c.licenseType(), c.siteId(), c.issuedAt(), c.notBefore(), c.notAfter(), c.warningNotice(), 1, c.solution(),
                c.total(), c.channels(), null, null, null, "text", "sha", "fp", null, 1, "admin", null, null, null, NOW);
        IssueForm copy = IssueForm.copyOf(rec, KST, java.time.LocalDate.of(2026, 10, 7));
        assertThat(copy.getChannels()).hasSize(IssueForm.MIN_ROWS);   // 6행 + 빈 행 → 최소 10
        assertThat(copy.getChannels().get(5).getGroup()).isEqualTo("streaming");
        assertThat(ContractValidator.validate(copy, KST, NOW).contract().channels()).isEqualTo(c.channels());
    }

    @Test
    void warnings() {
        IssueForm f = trial();
        f.setNotBeforeDate("2026-01-01");
        f.setNotAfterDate("2026-06-30");
        var r = ContractValidator.validate(f, KST, NOW);
        assertThat(r.errors()).isEmpty();
        assertThat(r.warnings()).hasSize(2); // 과거 시작, 이미 종료
        f = valid();
        f.setTotal(40);                      // 엔진 합 26 < total 40
        assertThat(ContractValidator.validate(f, KST, NOW).warnings()).anyMatch(w -> w.contains("total"));
    }

    @Test
    void contractJsonIsEscapedBySerializer() {
        IssueForm f = valid();
        f.setProjectName("Quote \" and \\ back 한글");
        String json = new String(ContractValidator.validate(f, KST, NOW).contract().toJson("0".repeat(32)),
                java.nio.charset.StandardCharsets.UTF_8);
        assertThat(json).contains("\"project_name\":\"Quote \\\" and \\\\ back 한글\"");
        assertThat(json).startsWith("{\"license_ver\":1,\"license_id\":\"");
        assertThat(json).contains("\"allowed_channels\":{\"total\":16,\"offline\":{\"sonastt\":8,\"sonaml\":4,\"sonaasr\":4},"
                + "\"online\":{\"sonastt\":8,\"sonaasr\":2}}");
        assertThat(json).contains("\"not_after\":\"9999-12-31T23:59:59Z\"");
        // 서명 대상은 규격(license_ver 1) 키만: 최상위·validity 키 집합과 순서를 정확히 비교
        java.util.Map<String, Object> root = tools.jackson.databind.json.JsonMapper.builder().build()
                .readValue(json, new tools.jackson.core.type.TypeReference<java.util.LinkedHashMap<String, Object>>() {});
        assertThat(root.keySet()).containsExactly("license_ver", "license_id", "project_name", "license_type", "solution",
                "site_id", "allowed_channels", "validity", "warning_notice");
        assertThat(new java.util.ArrayList<Object>(((java.util.Map<?, ?>) root.get("validity")).keySet()))
                .containsExactly("issued_at", "not_before", "not_after");
    }
}

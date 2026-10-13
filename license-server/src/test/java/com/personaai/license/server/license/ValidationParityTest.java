package com.personaai.license.server.license;

import static org.assertj.core.api.Assertions.assertThat;

import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;
import java.util.stream.Stream;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import com.personaai.license.server.TestFixture;
import com.personaai.license.server.licensectl.Licensectl;

import tools.jackson.databind.json.JsonMapper;

/**
 * 서버 검증(ContractValidator)과 최종 판정(licensectl SignContract)이 경계값에서 같은 결론을 내는지 확인한다.
 * 채널 항목은 이름 자유(allowed_channels 의 total 제외)라 항목 이름·수 경계도 함께 본다.
 * 서버가 통과시킨 입력은 licensectl 도 통과해야 한다(서버가 더 느슨하면 발급 단계에서야 실패 → UX 저하).
 * 서버가 의도적으로 더 엄격한 경우(STRICTER)는 "서버 거부 · licensectl 통과"를 기대값으로 둔다.
 */
class ValidationParityTest {
    static final ZoneId KST = ZoneId.of("Asia/Seoul");
    static final Instant NOW = Instant.parse("2026-09-29T00:00:00Z");
    static final TestFixture F = TestFixture.get();
    static final Licensectl L = new LicensectlAccess().create();
    static final JsonMapper JSON = JsonMapper.builder().build();

    enum Expect { SAME, STRICTER }

    static Arguments c(String name, Consumer<IssueForm> edit) {
        return Arguments.of(name, edit, Expect.SAME);
    }

    static Arguments stricter(String name, Consumer<IssueForm> edit) {
        return Arguments.of(name, edit, Expect.STRICTER);
    }

    static List<IssueForm.ChannelRow> items(String group, int n, int each) {
        List<IssueForm.ChannelRow> r = new ArrayList<>();
        for (int i = 0; i < n; i++) r.add(new IssueForm.ChannelRow(group, "e" + (100 + i), each));
        return r;
    }

    static List<IssueForm.ChannelRow> groups(int n) {
        List<IssueForm.ChannelRow> r = new ArrayList<>();
        for (int i = 0; i < n; i++) r.add(new IssueForm.ChannelRow("g" + (100 + i), "sonastt", 1));
        return r;
    }

    /** 기본 행: 0~2 offline(sonastt 8, sonaml 4, sonaasr 4), 3~4 online(sonastt 8, sonaasr 2), 5~9 빈 행. */
    static void row(IssueForm f, int i, String group, String name, Integer count) {
        f.getChannels().set(i, new IssueForm.ChannelRow(group, name, count));
    }

    static Stream<Arguments> cases() {
        return Stream.of(
                c("baseline production (permanent)", f -> {}),
                c("baseline trial", f -> f.setLicenseType("trial")),
                c("type dev", f -> f.setLicenseType("dev")),
                c("type demo (alias not accepted)", f -> f.setLicenseType("demo")),
                c("type Production (case)", f -> f.setLicenseType("Production")),
                c("type unknown", f -> f.setLicenseType("enterprise")),
                c("production ignores dated end", f -> { f.setNotAfterDate("2027-01-01"); }),
                c("total 0", f -> f.setTotal(0)),
                c("total 1 with item 1", f -> { f.setTotal(1); f.getChannels().forEach(r -> r.setCount(r.getName().isEmpty() ? 0 : 1)); }),
                stricter("count without item name", f -> row(f, 5, "offline", "", 1)),   // JSON 으로는 표현할 수 없는 입력 실수
                stricter("count without group name", f -> row(f, 5, "", "sonastt", 1)),
                c("total 100000", f -> f.setTotal(100_000)),
                c("total 100001", f -> f.setTotal(100_001)),
                c("item = total", f -> f.getChannels().get(0).setCount(16)),
                c("item > total", f -> f.getChannels().get(0).setCount(17)),
                c("item 100001", f -> { f.setTotal(100_000); f.getChannels().get(0).setCount(100_001); }),
                c("item name uppercase", f -> f.getChannels().get(0).setName("SonaSTT")),
                c("item name hyphen", f -> f.getChannels().get(0).setName("sona-stt")),
                c("item name 64 chars", f -> f.getChannels().get(0).setName("e".repeat(64))),
                c("item name 65 chars", f -> f.getChannels().get(0).setName("e".repeat(65))),
                c("custom group", f -> row(f, 5, "streaming", "sonastt", 3)),
                c("only custom group", f -> { f.getChannels().forEach(r -> r.setCount(0)); row(f, 5, "batch_gpu", "sonaml", 2); }),
                c("group name uppercase", f -> row(f, 5, "Streaming", "sonastt", 1)),
                c("group name 64 chars", f -> row(f, 5, "g".repeat(64), "sonastt", 1)),
                c("group name 65 chars", f -> row(f, 5, "g".repeat(65), "sonastt", 1)),
                c("group named total", f -> row(f, 5, "total", "sonastt", 1)),
                c("same item name in two groups", f -> row(f, 5, "streaming", "sonaml", 4)),
                c("all items zero", f -> f.getChannels().forEach(r -> r.setCount(0))),
                c("only online", f -> { for (int i = 0; i < 3; i++) f.getChannels().get(i).setCount(0); }),
                c("only offline", f -> { f.getChannels().get(3).setCount(0); f.getChannels().get(4).setCount(0); }),
                c("64 items in a group", f -> f.setChannels(items("offline", 64, 1))),
                c("65 items in a group", f -> f.setChannels(items("offline", 65, 1))),
                c("64 groups", f -> f.setChannels(groups(64))),
                c("65 groups", f -> f.setChannels(groups(65))),
                c("solution 128 bytes", f -> f.setSolution("s".repeat(128))),
                c("solution 129 bytes", f -> f.setSolution("s".repeat(129))),
                c("solution korean 126 bytes", f -> f.setSolution("솔".repeat(42))),
                c("solution korean 129 bytes", f -> f.setSolution("솔".repeat(43))),
                c("project 256 chars", f -> f.setProjectName("p".repeat(256))),
                c("project 257 chars", f -> f.setProjectName("p".repeat(257))),
                c("project korean 255 bytes", f -> f.setProjectName("가".repeat(85))),
                c("project korean 258 bytes", f -> f.setProjectName("가".repeat(86))),
                c("site 256 chars", f -> f.setSiteId("s".repeat(256))),
                c("site 257 chars", f -> f.setSiteId("s".repeat(257))),
                c("notice 4096", f -> f.setWarningNotice("n".repeat(4096))),
                c("notice 4097", f -> f.setWarningNotice("n".repeat(4097))),
                c("tab in project", f -> f.setProjectName("a\tb")),
                c("DEL in site", f -> f.setSiteId("a\u007Fb")),
                c("korean text", f -> f.setProjectName("음성인식 고도화 \"2026\"")),
                c("trial same instant", f -> { f.setLicenseType("trial"); f.setNotAfterDate(f.getNotBeforeDate()); f.setNotAfterTime(f.getNotBeforeTime()); }),
                c("trial one second", f -> { f.setLicenseType("trial"); f.setNotAfterDate(f.getNotBeforeDate()); f.setNotAfterTime("00:00:01"); }),
                // 서버가 의도적으로 더 엄격한 경우
                stricter("trial ends in year 9999", f -> { f.setLicenseType("trial"); f.setNotAfterDate("9999-12-31"); f.setNotAfterTime("23:59:59"); }),
                stricter("dev ends in year 9999", f -> { f.setLicenseType("dev"); f.setNotAfterDate("9999-12-31"); f.setNotAfterTime("23:59:59"); }),
                stricter("duplicate item row", f -> row(f, 5, "offline", "sonastt", 1)));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("cases")
    void serverAndLicensectlAgree(String name, Consumer<IssueForm> edit, Expect expect) {
        IssueForm f = ContractValidatorTest.valid();
        edit.accept(f);
        var v = ContractValidator.validate(f, KST, NOW);
        boolean serverOk = v.ok();
        boolean coreOk;
        byte[] json = serverOk ? v.contract().toJson("0123456789abcdef0123456789abcdef") : rawJson(f);
        try {
            L.sign(F.privateKeyLine.getBytes(StandardCharsets.US_ASCII), json);
            coreOk = true;
        } catch (Licensectl.LicensectlException e) {
            coreOk = false;
        }
        if (expect == Expect.SAME) {
            assertThat(serverOk).as("server=%s licensectl=%s errors=%s", serverOk, coreOk, v.errors()).isEqualTo(coreOk);
        } else {
            assertThat(serverOk).as("server must reject: %s", v.errors()).isFalse();
            assertThat(coreOk).as("licensectl accepts (server is stricter by design)").isTrue();
        }
    }

    /**
     * 서버가 거부한 입력도 licensectl 에 그대로 넣어 본다(서버가 너무 엄격하지 않은지 확인).
     * 서버와 같은 변환(운영 → 영구, 이름 빈 행·0 행 제외)만 하고 값 검증은 하지 않는다.
     */
    static byte[] rawJson(IssueForm f) {
        Instant nb, na;
        try {
            nb = java.time.LocalDateTime.parse(f.getNotBeforeDate() + "T" + f.getNotBeforeTime()).atZone(KST).toInstant();
            na = "production".equals(f.getLicenseType()) ? Contract.PERMANENT
                    : java.time.LocalDateTime.parse(f.getNotAfterDate() + "T" + f.getNotAfterTime()).atZone(KST).toInstant();
        } catch (Exception e) {
            return "{}".getBytes();
        }
        Map<String, Object> channels = new LinkedHashMap<>();
        channels.put("total", f.getTotal());
        channels.putAll(raw(f.getChannels()));
        Map<String, Object> validity = new LinkedHashMap<>();
        validity.put("issued_at", Contract.utc(NOW));
        validity.put("not_before", Contract.utc(nb));
        validity.put("not_after", Contract.utc(na));
        Map<String, Object> root = new LinkedHashMap<>();
        root.put("license_ver", 1);
        root.put("license_id", "0123456789abcdef0123456789abcdef");
        root.put("project_name", f.getProjectName());
        root.put("license_type", f.getLicenseType());
        root.put("solution", f.getSolution());
        root.put("site_id", f.getSiteId());
        root.put("allowed_channels", channels);
        root.put("validity", validity);
        root.put("warning_notice", f.getWarningNotice());
        return JSON.writeValueAsBytes(root);
    }

    /** 이름이 모두 있고 상한이 0 이 아닌 행만 {항목: {세부 항목: 상한}} 으로. 'total' 항목도 그대로 넣는다(코어가 판정). */
    static Map<String, Object> raw(List<IssueForm.ChannelRow> rows) {
        Map<String, Object> m = new LinkedHashMap<>();
        for (IssueForm.ChannelRow r : rows) {
            String g = r.getGroup() == null ? "" : r.getGroup().strip();
            String n = r.getName() == null ? "" : r.getName().strip();
            if (g.isEmpty() || n.isEmpty() || r.getCount() == null || r.getCount() == 0) continue;
            Object cur = m.get(g);
            @SuppressWarnings("unchecked")
            Map<String, Integer> items = cur instanceof Map ? (Map<String, Integer>) cur : new LinkedHashMap<>();
            items.put(n, r.getCount());
            m.put(g, items);
        }
        return m;
    }

    static final class LicensectlAccess {
        Licensectl create() {
            return com.personaai.license.server.licensectl.LicensectlTestAccess.create(TestFixture.LICENSECTL, Duration.ofSeconds(10));
        }
    }
}

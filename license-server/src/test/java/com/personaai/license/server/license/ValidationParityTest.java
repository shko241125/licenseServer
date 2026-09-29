package com.personaai.license.server.license;

import static org.assertj.core.api.Assertions.assertThat;

import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.time.Instant;
import java.time.ZoneId;
import java.util.function.Consumer;
import java.util.stream.Stream;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;

import com.personaai.license.server.TestFixture;
import com.personaai.license.server.licensectl.Licensectl;

/**
 * 서버 검증(ContractValidator)과 최종 판정(licensectl SignContract)이 경계값에서 같은 결론을 내는지 확인한다.
 * 서버가 통과시킨 입력은 licensectl 도 통과해야 한다(서버가 더 느슨하면 발급 단계에서야 실패 → UX 저하).
 */
class ValidationParityTest {
    static final ZoneId KST = ZoneId.of("Asia/Seoul");
    static final Instant NOW = Instant.parse("2026-09-29T00:00:00Z");
    static final TestFixture F = TestFixture.get();
    static final Licensectl L = new LicensectlAccess().create();

    static Arguments c(String name, Consumer<IssueForm> edit) {
        return Arguments.of(name, edit);
    }

    static Stream<Arguments> cases() {
        return Stream.of(
                c("baseline", f -> {}),
                c("channels max", f -> { f.setOnlineStt(100_000); f.setOfflineStt(100_000); }),
                c("channels over", f -> f.setOfflineStt(100_001)),
                c("channels both zero", f -> { f.setOnlineStt(0); f.setOfflineStt(0); }),
                c("only online", f -> { f.setOnlineStt(1); f.setOfflineStt(0); }),
                c("grace 0", f -> f.setGracePeriodDays(0)),
                c("grace 90", f -> f.setGracePeriodDays(90)),
                c("grace 91", f -> f.setGracePeriodDays(91)),
                c("same instant", f -> { f.setNotAfterDate(f.getNotBeforeDate()); f.setNotAfterTime(f.getNotBeforeTime()); }),
                c("one second", f -> { f.setNotAfterDate(f.getNotBeforeDate()); f.setNotAfterTime("00:00:01"); }),
                c("project 256 chars", f -> f.setProjectName("p".repeat(256))),
                c("project 257 chars", f -> f.setProjectName("p".repeat(257))),
                c("site 256 chars", f -> f.setSiteId("s".repeat(256))),
                c("site 257 chars", f -> f.setSiteId("s".repeat(257))),
                c("notice 4096", f -> f.setWarningNotice("n".repeat(4096))),
                c("notice 4097", f -> f.setWarningNotice("n".repeat(4097))),
                c("tab in project", f -> f.setProjectName("a\tb")),
                c("DEL in site", f -> f.setSiteId("a\u007Fb")),
                c("korean text", f -> f.setProjectName("음성인식 고도화 \"2026\"")),
                c("type trial", f -> f.setLicenseType("trial")),
                c("type unknown", f -> f.setLicenseType("enterprise")),
                c("year 9999", f -> { f.setNotAfterDate("9999-12-31"); f.setNotAfterTime("23:59:59"); }));
    }

    @ParameterizedTest(name = "{0}")
    @MethodSource("cases")
    void serverAndLicensectlAgree(String name, Consumer<IssueForm> edit) {
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
        assertThat(serverOk).as("server=%s licensectl=%s errors=%s", serverOk, coreOk, v.errors()).isEqualTo(coreOk);
    }

    /** 서버가 거부한 입력도 licensectl 에 그대로 넣어 본다(서버가 너무 엄격하지 않은지 확인). */
    static byte[] rawJson(IssueForm f) {
        Instant nb, na;
        try {
            nb = java.time.LocalDateTime.parse(f.getNotBeforeDate() + "T" + f.getNotBeforeTime()).atZone(KST).toInstant();
            na = java.time.LocalDateTime.parse(f.getNotAfterDate() + "T" + f.getNotAfterTime()).atZone(KST).toInstant();
        } catch (Exception e) {
            return "{}".getBytes();
        }
        return new Contract(f.getProjectName(), f.getLicenseType(), f.getSiteId(), f.getOnlineStt(), f.getOfflineStt(),
                NOW, nb, na, f.getGracePeriodDays(), f.getWarningNotice(), null).toJson("0123456789abcdef0123456789abcdef");
    }

    static final class LicensectlAccess {
        Licensectl create() {
            return com.personaai.license.server.licensectl.LicensectlTestAccess.create(TestFixture.LICENSECTL, Duration.ofSeconds(10));
        }
    }
}

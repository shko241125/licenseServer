package com.personaai.license.server.license;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.UUID;
import java.util.concurrent.Callable;
import java.util.concurrent.Executors;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.jdbc.core.simple.JdbcClient;

import com.personaai.license.server.IntegrationTestBase;
import com.personaai.license.server.TestFixture;
import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.key.KeyVault;
import com.personaai.license.server.security.AppPrincipal;
import com.personaai.license.server.user.UserRepository;

class IssueFlowIT extends IntegrationTestBase {
    @Autowired IssueService issue;
    @Autowired KeyVault vault;
    @Autowired UserRepository users;
    @Autowired AuditService audit;
    @Autowired JdbcClient jdbc;

    AppPrincipal admin() {
        return new AppPrincipal(users.findByUsername("root.admin").orElseThrow(), false);
    }

    static IssueForm form() {
        IssueForm f = ContractValidatorTest.valid();
        f.setNotBeforeDate("2026-09-01"); // 현재 유효한 라이선스 (verify 종료 코드 0)
        f.setNotAfterDate("2099-09-30");
        return f;
    }

    @AfterEach
    void sealAgain() {
        vault.seal("test", null, "test cleanup");
    }

    @Test
    void sealedKeyBlocksIssuance() {
        assertThat(vault.state()).isEqualTo(KeyVault.State.SEALED);
        assertThatThrownBy(() -> issue.issue(form(), admin(), "127.0.0.1")).hasMessageContaining("봉인");
    }

    @Test
    void wrongPassphraseThenLockout() {
        for (int i = 0; i < 5; i++) {
            assertThat(vault.unseal("wrong passphrase for sure!!".toCharArray(), "t", null))
                    .isEqualTo(KeyVault.UnsealResult.WRONG_PASSPHRASE);
        }
        assertThat(vault.unseal(TestFixture.PASSPHRASE.toCharArray(), "t", null)).isEqualTo(KeyVault.UnsealResult.LOCKED);
        assertThat(vault.lockedUntil()).isNotNull();
        org.springframework.test.util.ReflectionTestUtils.setField(vault, "lockedUntil", null); // 다른 테스트를 위해 해제
    }

    @Test
    void issueVerifyDownloadVoid() throws Exception {
        char[] pass = TestFixture.PASSPHRASE.toCharArray();
        assertThat(vault.unseal(pass, "t", null)).isEqualTo(KeyVault.UnsealResult.OK);
        assertThat(pass).containsOnly('\0'); // 호출 후 패스프레이즈 배열을 지움

        IssueForm f = form();
        f.setProjectName("통합 테스트 \"프로젝트\"");
        LicenseRecord r = issue.issue(f, admin(), "127.0.0.1");
        assertThat(r.status()).isEqualTo(LicenseRecord.Status.ISSUED);
        assertThat(r.licenseText()).contains("\"license_id\": \"" + r.licenseId() + "\"");
        assertThat(r.issuedByName()).isEqualTo("root.admin");

        // 서버와 무관한 경로(licensectl verify + public.key 파일)로 재검증
        Path lic = Files.createTempFile("issued", ".lic");
        Files.writeString(lic, r.licenseText(), StandardCharsets.UTF_8);
        Process p = new ProcessBuilder(TestFixture.LICENSECTL.toString(), "verify", "-pub",
                F.dir.resolve("keys/public.key").toString(), lic.toString()).redirectErrorStream(true).start();
        String out = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        assertThat(p.waitFor()).as(out).isZero();
        assertThat(out).contains("SIGNATURE OK").contains("offline_stt=8");

        // 같은 제출 토큰 재전송 → 새로 발급하지 않고 같은 건 반환
        assertThat(issue.issue(f, admin(), "127.0.0.1").licenseId()).isEqualTo(r.licenseId());

        assertThat(issue.voidLicense(r.licenseId(), "계약 해지", admin(), "127.0.0.1")).isTrue();
        assertThat(issue.voidLicense(r.licenseId(), "again", admin(), "127.0.0.1")).isFalse();
        assertThat(audit.verifyChain().ok()).isTrue();
    }

    @Test
    void concurrentDuplicateSubmitIssuesOnce() throws Exception {
        assertThat(vault.unseal(TestFixture.PASSPHRASE.toCharArray(), "t", null)).isEqualTo(KeyVault.UnsealResult.OK);
        IssueForm f = form();
        UUID token = UUID.randomUUID();
        f.setSubmissionToken(token);
        Callable<String> task = () -> issue.issue(f, admin(), "127.0.0.1").licenseId();
        try (var ex = Executors.newFixedThreadPool(4)) {
            var futures = ex.invokeAll(java.util.List.of(task, task, task, task));
            for (var fu : futures) fu.get();
        }
        long rows = jdbc.sql("SELECT count(*) FROM license WHERE submission_token = ?").param(token).query(Long.class).single();
        assertThat(rows).isEqualTo(1);
    }

    @Test
    void autoResealAfterIdle() {
        assertThat(vault.unseal(TestFixture.PASSPHRASE.toCharArray(), "t", null)).isEqualTo(KeyVault.UnsealResult.OK);
        org.springframework.test.util.ReflectionTestUtils.setField(vault, "lastUsed", java.time.Instant.now().minusSeconds(9 * 3600));
        vault.autoReseal();
        assertThat(vault.state()).isEqualTo(KeyVault.State.SEALED);
    }
}

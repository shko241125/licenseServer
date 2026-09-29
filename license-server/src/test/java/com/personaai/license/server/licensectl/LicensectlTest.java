package com.personaai.license.server.licensectl;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.time.Duration;
import java.util.Map;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

import com.personaai.license.server.TestFixture;
import com.personaai.license.server.licensectl.Licensectl.LicensectlException;

class LicensectlTest {
    static final TestFixture F = TestFixture.get();
    final Licensectl real = new Licensectl(TestFixture.LICENSECTL, Duration.ofSeconds(10));

    static final String CONTRACT = """
            {"format_version":1,"license_id":"0123456789abcdef0123456789abcdef","project_name":"P","license_type":"poc",
             "site_id":"S","allowed_channels":{"online_stt":0,"offline_stt":3},
             "validity":{"issued_at":"2026-09-29T00:00:00Z","not_before":"2026-09-29T00:00:00Z",
                         "not_after":"2099-01-01T00:00:00Z","grace_period_days":0},"warning_notice":"w"}""";

    @Test
    void signVerifyPubkey() {
        byte[] key = F.privateKeyLine.getBytes(StandardCharsets.US_ASCII);
        String lic = real.sign(key, CONTRACT.getBytes(StandardCharsets.UTF_8));
        assertThat(lic).startsWith("{\n  \"format_version\": 1,").contains("\"signature\": \"");
        Map<String, Object> v = real.verify(F.publicKey, lic.getBytes(StandardCharsets.UTF_8));
        assertThat(v).containsEntry("result", "OK").containsEntry("state", "VALID").containsEntry("offline_stt", 3);
        assertThat(real.publicKeyOf(key)).isEqualTo(F.publicKey);
        Map<String, Object> bad = real.verify(F.publicKey, lic.replace("\"offline_stt\": 3", "\"offline_stt\": 30").getBytes());
        assertThat(bad).containsEntry("result", "LICENSE_BAD_SIGNATURE");
    }

    @Test
    void signErrorIsParsed() {
        assertThatThrownBy(() -> real.sign(F.privateKeyLine.getBytes(), CONTRACT.replace("\"poc\"", "\"x\"").getBytes()))
                .isInstanceOf(LicensectlException.class)
                .satisfies(e -> assertThat(((LicensectlException) e).errorName()).isEqualTo("LICENSE_INVALID_FIELD"));
        assertThatThrownBy(() -> real.sign(F.publicKey.getBytes(), CONTRACT.getBytes()))
                .hasMessageContaining("not a licensectl private key");
    }

    static Licensectl script(Path dir, String body, Duration timeout) throws Exception {
        Path s = dir.resolve("fake.sh");
        Files.writeString(s, "#!/bin/sh\n" + body + "\n");
        Files.setPosixFilePermissions(s, PosixFilePermissions.fromString("rwx------"));
        return new Licensectl(s, timeout);
    }

    @Test
    void environmentIsCleared(@TempDir Path dir) throws Exception {
        Licensectl.Result r = script(dir, "env", Duration.ofSeconds(5)).run(new byte[0], "x");
        assertThat(r.stdout()).doesNotContain("HOME=").doesNotContain("LD_");
    }

    @Test
    void timeoutAndOutputCap(@TempDir Path dir) throws Exception {
        assertThatThrownBy(() -> script(dir, "sleep 30", Duration.ofMillis(500)).run(new byte[0], "x"))
                .satisfies(e -> assertThat(((LicensectlException) e).errorName()).isEqualTo("LICENSECTL_TIMEOUT"));
        Files.delete(dir.resolve("fake.sh"));
        assertThatThrownBy(() -> script(dir, "head -c 400000 /dev/zero", Duration.ofSeconds(5)).run(new byte[0], "x"))
                .satisfies(e -> assertThat(((LicensectlException) e).errorName()).isEqualTo("LICENSECTL_IO"));
    }

    @Test
    void missingExecutable() {
        assertThatThrownBy(() -> new Licensectl(Path.of("/nonexistent/licensectl"), Duration.ofSeconds(1)).run(new byte[0], "x"))
                .satisfies(e -> assertThat(((LicensectlException) e).errorName()).isEqualTo("LICENSECTL_UNAVAILABLE"));
    }
}

package com.personaai.license.server;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Base64;

import com.personaai.license.server.crypto.Crypto;
import com.personaai.license.server.key.SealedKeyFile;

/** 테스트용 실제 키: licensectl keygen → 봉인 파일, TOTP 데이터 키, 초기 관리자 비밀번호 파일. */
public final class TestFixture {
    public static final String PASSPHRASE = "correct horse battery staple 2026";
    public static final String BOOTSTRAP_PASSWORD = "Bootstrap-Pass-2026!";
    public static final Path LICENSECTL = Path.of(System.getProperty("licensectl.path", "../build-release/licensectl"));

    public final Path dir;
    public final Path sealedKey;
    public final Path dataKey;
    public final Path bootstrapPassword;
    public final String privateKeyLine;
    public final String publicKey;

    private static TestFixture instance;

    public static synchronized TestFixture get() {
        if (instance == null) instance = create();
        return instance;
    }

    private TestFixture(Path dir, Path sealedKey, Path dataKey, Path bootstrapPassword, String priv, String pub) {
        this.dir = dir;
        this.sealedKey = sealedKey;
        this.dataKey = dataKey;
        this.bootstrapPassword = bootstrapPassword;
        this.privateKeyLine = priv;
        this.publicKey = pub;
    }

    private static TestFixture create() {
        try {
            if (!Files.isExecutable(LICENSECTL)) {
                throw new IllegalStateException("licensectl not found at " + LICENSECTL + " (build it or pass -Plicensectl=...)");
            }
            Path dir = Files.createTempDirectory("license-server-test");
            Process p = new ProcessBuilder(LICENSECTL.toString(), "keygen", "-out", dir.resolve("keys").toString())
                    .redirectErrorStream(true).start();
            p.getInputStream().readAllBytes();
            if (p.waitFor() != 0) throw new IllegalStateException("keygen failed");
            String priv = Files.readString(dir.resolve("keys/private.key")).strip();
            String pub = Files.readString(dir.resolve("keys/public.key")).strip();
            Path sealed = dir.resolve("sealed-key.json");
            Files.writeString(sealed, SealedKeyFile.seal(priv.getBytes(StandardCharsets.US_ASCII),
                    PASSPHRASE.toCharArray(), pub, 100_000).toJson());
            Path dataKey = dir.resolve("data_key");
            Files.writeString(dataKey, Base64.getEncoder().encodeToString(Crypto.randomBytes(32)));
            Path boot = dir.resolve("initial_admin_password");
            Files.writeString(boot, BOOTSTRAP_PASSWORD);
            return new TestFixture(dir, sealed, dataKey, boot, priv, pub);
        } catch (Exception e) {
            throw new IllegalStateException(e);
        }
    }
}

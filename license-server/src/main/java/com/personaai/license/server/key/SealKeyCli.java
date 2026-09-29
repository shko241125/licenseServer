package com.personaai.license.server.key;

import java.io.Console;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;

import com.personaai.license.server.config.LicenseProperties;
import com.personaai.license.server.licensectl.Licensectl;

/**
 * 키 생성식용 CLI (Spring 을 띄우지 않는다).
 *   java -jar license-server.jar seal-key --in private.key --out sealed-key.json
 *        [--licensectl /opt/licensectl] [--passphrase-file f]
 * 패스프레이즈는 콘솔에서 두 번 입력받는다(파일 옵션은 자동화·테스트용).
 */
public final class SealKeyCli {
    private SealKeyCli() {}

    public static int run(String[] args) throws Exception {
        Map<String, String> opt = new HashMap<>();
        for (int i = 1; i + 1 < args.length; i += 2) opt.put(args[i], args[i + 1]);
        if (!opt.containsKey("--in") || !opt.containsKey("--out") || args.length % 2 == 0) {
            System.err.println("usage: seal-key --in <private.key> --out <sealed-key.json> "
                    + "[--licensectl <path>] [--passphrase-file <file>]");
            return 2;
        }
        Path in = Path.of(opt.get("--in"));
        Path out = Path.of(opt.get("--out"));
        Path licensectlPath = Path.of(opt.getOrDefault("--licensectl", "/opt/licensectl"));

        byte[] line = Files.readString(in, StandardCharsets.US_ASCII).strip().getBytes(StandardCharsets.US_ASCII);
        char[] pass = null;
        try {
            pass = opt.containsKey("--passphrase-file") ? readPassphraseFile(Path.of(opt.get("--passphrase-file")))
                    : readPassphraseTwice();
            if (pass == null) return 1;
            Licensectl licensectl = new Licensectl(new LicenseProperties(licensectlPath, null, null, null, null,
                    null, null, null, null));
            String publicKey = licensectl.publicKeyOf(line); // 비밀키 형식 검증 겸 공개키 계산
            SealedKeyFile sealed = SealedKeyFile.seal(line, pass, publicKey, SealedKeyFile.DEFAULT_ITERATIONS);
            if (!Arrays.equals(sealed.unseal(pass), line)) throw new IllegalStateException("self-check failed");
            Files.writeString(out, sealed.toJson(), StandardCharsets.UTF_8, StandardOpenOption.CREATE_NEW,
                    StandardOpenOption.WRITE);
            try {
                Files.setPosixFilePermissions(out, PosixFilePermissions.fromString("rw-------"));
            } catch (UnsupportedOperationException ignored) {
                // POSIX 권한이 없는 파일시스템
            }
            System.out.println("sealed key : " + out);
            System.out.println("public key : " + publicKey);
            System.out.println("fingerprint: " + sealed.publicKeyFingerprint() + "  (set LICENSE_PUBLIC_KEY_FP)");
            System.out.println("Now securely delete the plaintext private key file: " + in);
            return 0;
        } finally {
            Arrays.fill(line, (byte) 0);
            if (pass != null) Arrays.fill(pass, '\0');
        }
    }

    private static char[] readPassphraseTwice() {
        Console c = System.console();
        if (c == null) {
            System.err.println("no console; use --passphrase-file");
            return null;
        }
        char[] a = c.readPassword("passphrase (min %d chars): ", SealedKeyFile.MIN_PASSPHRASE_LENGTH);
        char[] b = c.readPassword("repeat passphrase: ");
        boolean same = a != null && Arrays.equals(a, b);
        if (b != null) Arrays.fill(b, '\0');
        if (!same) {
            if (a != null) Arrays.fill(a, '\0');
            System.err.println("passphrases do not match");
            return null;
        }
        return a;
    }

    private static char[] readPassphraseFile(Path p) throws java.io.IOException {
        return Files.readString(p, StandardCharsets.UTF_8).strip().toCharArray();
    }
}

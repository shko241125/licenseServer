package com.personaai.license.server.key;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.GeneralSecurityException;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Arrays;
import java.util.Base64;

import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;

import com.personaai.license.server.crypto.Crypto;

import tools.jackson.databind.JsonNode;
import tools.jackson.databind.json.JsonMapper;
import tools.jackson.databind.node.ObjectNode;

/**
 * 봉인된 서명 비밀키 파일 (stt-license-sealed-key-v1).
 * 평문 = licensectl 비밀키 문자열("stt-license-ed25519-private-v1:..."), 암호 = AES-256-GCM,
 * 키 = PBKDF2-HMAC-SHA256(패스프레이즈). 헤더 필드 전체를 AAD 로 묶어 공개키 등 필드 바꿔치기를 탐지한다.
 */
public record SealedKeyFile(
        String format, String kdf, int iterations, byte[] salt, String cipher,
        byte[] nonce, byte[] ciphertext, String publicKey, String createdAt) {

    public static final String FORMAT = "stt-license-sealed-key-v1";
    public static final String KDF = "PBKDF2-HMAC-SHA256";
    public static final String CIPHER = "AES-256-GCM";
    /** OWASP Password Storage Cheat Sheet 의 PBKDF2-HMAC-SHA256 권장 반복 횟수. */
    public static final int DEFAULT_ITERATIONS = 600_000;
    public static final int MIN_PASSPHRASE_LENGTH = 20;

    private static final JsonMapper JSON = JsonMapper.builder().build();
    private static final Base64.Encoder B64 = Base64.getEncoder();
    private static final Base64.Decoder B64D = Base64.getDecoder();

    public static SealedKeyFile seal(byte[] privateKeyLine, char[] passphrase, String publicKey, int iterations) {
        if (passphrase.length < MIN_PASSPHRASE_LENGTH) {
            throw new IllegalArgumentException("passphrase must be at least " + MIN_PASSPHRASE_LENGTH + " characters");
        }
        byte[] salt = Crypto.randomBytes(16);
        String createdAt = Instant.now().truncatedTo(ChronoUnit.SECONDS).toString();
        SealedKeyFile header = new SealedKeyFile(FORMAT, KDF, iterations, salt, CIPHER, null, null, publicKey, createdAt);
        byte[] key = deriveKey(passphrase, salt, iterations);
        try {
            byte[] sealed = Crypto.gcmEncrypt(key, privateKeyLine, header.aad());
            return new SealedKeyFile(FORMAT, KDF, iterations, salt, CIPHER,
                    Arrays.copyOfRange(sealed, 0, Crypto.GCM_NONCE_BYTES),
                    Arrays.copyOfRange(sealed, Crypto.GCM_NONCE_BYTES, sealed.length), publicKey, createdAt);
        } finally {
            Arrays.fill(key, (byte) 0);
        }
    }

    /** 복호화한 비밀키 문자열 바이트. 패스프레이즈가 틀리거나 파일이 변조되면 GeneralSecurityException. */
    public byte[] unseal(char[] passphrase) throws GeneralSecurityException {
        byte[] key = deriveKey(passphrase, salt, iterations);
        try {
            byte[] input = new byte[nonce.length + ciphertext.length];
            System.arraycopy(nonce, 0, input, 0, nonce.length);
            System.arraycopy(ciphertext, 0, input, nonce.length, ciphertext.length);
            return Crypto.gcmDecrypt(key, input, aad());
        } finally {
            Arrays.fill(key, (byte) 0);
        }
    }

    /** 공개키(Base64 32바이트)의 SHA-256 지문(hex). */
    public String publicKeyFingerprint() {
        return fingerprint(publicKey);
    }

    public static String fingerprint(String publicKeyB64) {
        return Crypto.sha256Hex(B64D.decode(publicKeyB64));
    }

    byte[] aad() {
        return String.join("\n", format, kdf, Integer.toString(iterations), B64.encodeToString(salt),
                cipher, publicKey, createdAt).getBytes(StandardCharsets.UTF_8);
    }

    private static byte[] deriveKey(char[] passphrase, byte[] salt, int iterations) {
        PBEKeySpec spec = new PBEKeySpec(passphrase, salt, iterations, 256);
        try {
            return SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException(e);
        } finally {
            spec.clearPassword();
        }
    }

    public String toJson() {
        ObjectNode o = JSON.createObjectNode();
        o.put("format", format);
        o.put("kdf", kdf);
        o.put("iterations", iterations);
        o.put("salt", B64.encodeToString(salt));
        o.put("cipher", cipher);
        o.put("nonce", B64.encodeToString(nonce));
        o.put("ciphertext", B64.encodeToString(ciphertext));
        o.put("public_key", publicKey);
        o.put("created_at", createdAt);
        return JSON.writerWithDefaultPrettyPrinter().writeValueAsString(o) + "\n";
    }

    public static SealedKeyFile parse(String json) {
        JsonNode n = JSON.readTree(json);
        String format = text(n, "format");
        if (!FORMAT.equals(format) || !KDF.equals(text(n, "kdf")) || !CIPHER.equals(text(n, "cipher"))) {
            throw new IllegalArgumentException("unsupported sealed key file (format/kdf/cipher)");
        }
        JsonNode it = n.get("iterations");
        if (it == null || !it.isInt() || it.intValue() < 100_000) {
            throw new IllegalArgumentException("sealed key file: iterations missing or too low");
        }
        String publicKey = text(n, "public_key");
        if (B64D.decode(publicKey).length != 32) throw new IllegalArgumentException("public_key must be 32 bytes");
        return new SealedKeyFile(format, text(n, "kdf"), it.intValue(), B64D.decode(text(n, "salt")),
                text(n, "cipher"), B64D.decode(text(n, "nonce")), B64D.decode(text(n, "ciphertext")),
                publicKey, text(n, "created_at"));
    }

    public static SealedKeyFile read(Path path) throws java.io.IOException {
        return parse(Files.readString(path, StandardCharsets.UTF_8));
    }

    private static String text(JsonNode n, String field) {
        JsonNode v = n.get(field);
        if (v == null || !v.isString()) throw new IllegalArgumentException("sealed key file: missing " + field);
        return v.stringValue();
    }
}

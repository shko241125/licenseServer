package com.personaai.license.server.crypto;

import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.HexFormat;

import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/** JDK 표준만 쓰는 작은 암호 도우미. */
public final class Crypto {
    public static final SecureRandom RANDOM = new SecureRandom();
    private static final int GCM_TAG_BITS = 128;
    public static final int GCM_NONCE_BYTES = 12;

    private Crypto() {}

    public static byte[] randomBytes(int n) {
        byte[] b = new byte[n];
        RANDOM.nextBytes(b);
        return b;
    }

    public static String sha256Hex(byte[] data) {
        try {
            return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(data));
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException(e);
        }
    }

    public static String sha256Hex(String s) {
        return sha256Hex(s.getBytes(StandardCharsets.UTF_8));
    }

    /** AES-256-GCM 암호화. 반환값 = nonce(12) || ciphertext+tag. */
    public static byte[] gcmEncrypt(byte[] key, byte[] plaintext, byte[] aad) {
        byte[] nonce = randomBytes(GCM_NONCE_BYTES);
        byte[] ct = gcm(Cipher.ENCRYPT_MODE, key, nonce, plaintext, aad);
        byte[] out = new byte[nonce.length + ct.length];
        System.arraycopy(nonce, 0, out, 0, nonce.length);
        System.arraycopy(ct, 0, out, nonce.length, ct.length);
        return out;
    }

    /** gcmEncrypt 의 역. 인증 실패 시 GeneralSecurityException. */
    public static byte[] gcmDecrypt(byte[] key, byte[] nonceAndCiphertext, byte[] aad)
            throws GeneralSecurityException {
        if (nonceAndCiphertext.length < GCM_NONCE_BYTES + GCM_TAG_BITS / 8) {
            throw new GeneralSecurityException("ciphertext too short");
        }
        byte[] nonce = java.util.Arrays.copyOfRange(nonceAndCiphertext, 0, GCM_NONCE_BYTES);
        byte[] ct = java.util.Arrays.copyOfRange(nonceAndCiphertext, GCM_NONCE_BYTES, nonceAndCiphertext.length);
        return gcmChecked(Cipher.DECRYPT_MODE, key, nonce, ct, aad);
    }

    static byte[] gcm(int mode, byte[] key, byte[] nonce, byte[] input, byte[] aad) {
        try {
            return gcmChecked(mode, key, nonce, input, aad);
        } catch (GeneralSecurityException e) {
            throw new IllegalStateException(e);
        }
    }

    static byte[] gcmChecked(int mode, byte[] key, byte[] nonce, byte[] input, byte[] aad)
            throws GeneralSecurityException {
        if (key.length != 32) throw new GeneralSecurityException("AES-256 key must be 32 bytes");
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(mode, new SecretKeySpec(key, "AES"), new GCMParameterSpec(GCM_TAG_BITS, nonce));
        if (aad != null) c.updateAAD(aad);
        return c.doFinal(input);
    }

    /** 시간 차이로 값이 새지 않는 비교. */
    public static boolean constantTimeEquals(String a, String b) {
        return MessageDigest.isEqual(a.getBytes(StandardCharsets.UTF_8), b.getBytes(StandardCharsets.UTF_8));
    }
}

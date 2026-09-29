package com.personaai.license.server.key;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.GeneralSecurityException;
import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.Arrays;
import java.util.Map;
import java.util.function.Function;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import com.personaai.license.server.audit.AuditService;
import com.personaai.license.server.config.LicenseProperties;
import com.personaai.license.server.crypto.Crypto;
import com.personaai.license.server.licensectl.Licensectl;

/**
 * 서명 비밀키 금고. 디스크에는 봉인 파일만 있고, 해제된 키는 이 객체의 메모리(byte[])에만 있다.
 * 재봉인·자동 재봉인 시 배열을 0 으로 덮어쓴다(JVM 특성상 GC 복사본까지 지운다고 보장하지는 않는다).
 */
@Component
public class KeyVault {
    private static final Logger log = LoggerFactory.getLogger(KeyVault.class);
    static final int MAX_FAILURES = 5;
    static final Duration FAILURE_LOCK = Duration.ofMinutes(15);

    public enum State { MISSING, SEALED, UNSEALED }

    public enum UnsealResult { OK, ALREADY_UNSEALED, WRONG_PASSPHRASE, LOCKED, KEY_MISMATCH, MISSING }

    private final LicenseProperties props;
    private final Licensectl licensectl;
    private final AuditService audit;
    private final Clock clock;

    private SealedKeyFile file;
    private String loadError;
    private byte[] privateKeyLine;
    private Instant unsealedAt;
    private Instant lastUsed;
    private int failures;
    private Instant lockedUntil;

    public KeyVault(LicenseProperties props, Licensectl licensectl, AuditService audit, Clock clock) {
        this.props = props;
        this.licensectl = licensectl;
        this.audit = audit;
        this.clock = clock;
        reload();
    }

    /** 봉인 파일을 다시 읽는다(기동 시). 해제 상태에는 영향이 없다. */
    public synchronized void reload() {
        try {
            if (!Files.isRegularFile(props.sealedKeyPath())) {
                file = null;
                loadError = "sealed key file not found: " + props.sealedKeyPath();
            } else {
                SealedKeyFile f = SealedKeyFile.read(props.sealedKeyPath());
                String pinned = props.expectedPublicKeyFingerprint();
                if (pinned != null && !pinned.isBlank() && !Crypto.constantTimeEquals(pinned.toLowerCase(), f.publicKeyFingerprint())) {
                    file = null;
                    loadError = "sealed key public key fingerprint does not match LICENSE_PUBLIC_KEY_FP";
                } else {
                    file = f;
                    loadError = null;
                }
            }
        } catch (Exception e) {
            file = null;
            loadError = "cannot read sealed key file: " + e.getMessage();
        }
        if (loadError != null) log.warn("key vault: {}", loadError);
    }

    public synchronized State state() {
        if (file == null) return State.MISSING;
        return privateKeyLine == null ? State.SEALED : State.UNSEALED;
    }

    public synchronized String loadError() {
        return loadError;
    }

    public synchronized String publicKey() {
        return file == null ? null : file.publicKey();
    }

    public synchronized String publicKeyFingerprint() {
        return file == null ? null : file.publicKeyFingerprint();
    }

    public synchronized Instant unsealedAt() {
        return unsealedAt;
    }

    public synchronized Instant lockedUntil() {
        return lockedUntil != null && lockedUntil.isAfter(clock.instant()) ? lockedUntil : null;
    }

    public synchronized UnsealResult unseal(char[] passphrase, String actor, String ip) {
        try {
            if (file == null) return UnsealResult.MISSING;
            if (privateKeyLine != null) return UnsealResult.ALREADY_UNSEALED;
            if (lockedUntil() != null) {
                audit.record(actor, "UNSEAL_LOCKED", null, ip, Map.of());
                return UnsealResult.LOCKED;
            }
            byte[] line;
            try {
                line = file.unseal(passphrase);
            } catch (GeneralSecurityException e) {
                failures++;
                if (failures >= MAX_FAILURES) {
                    lockedUntil = clock.instant().plus(FAILURE_LOCK);
                    failures = 0;
                }
                audit.record(actor, "UNSEAL_FAIL", null, ip, Map.of("reason", "wrong passphrase or tampered file"));
                return UnsealResult.WRONG_PASSPHRASE;
            }
            // 복호화한 키가 파일에 적힌 공개키와 짝인지 licensectl 로 확인한다.
            String derived;
            try {
                derived = licensectl.publicKeyOf(line);
            } catch (RuntimeException e) {
                Arrays.fill(line, (byte) 0);
                throw e;
            }
            if (!Crypto.constantTimeEquals(derived, file.publicKey())) {
                Arrays.fill(line, (byte) 0);
                audit.record(actor, "UNSEAL_FAIL", null, ip, Map.of("reason", "private key does not match public key"));
                return UnsealResult.KEY_MISMATCH;
            }
            privateKeyLine = line;
            failures = 0;
            unsealedAt = lastUsed = clock.instant();
            audit.record(actor, "UNSEAL", null, ip, Map.of("public_key_fp", file.publicKeyFingerprint()));
            return UnsealResult.OK;
        } finally {
            Arrays.fill(passphrase, '\0');
        }
    }

    public synchronized void seal(String actor, String ip, String reason) {
        if (privateKeyLine == null) return;
        Arrays.fill(privateKeyLine, (byte) 0);
        privateKeyLine = null;
        unsealedAt = null;
        audit.record(actor, "SEAL", null, ip, Map.of("reason", reason));
    }

    /** 해제된 키로 작업한다. 봉인 상태면 IllegalStateException. 키 배열은 콜백 밖으로 보관하지 말 것. */
    public synchronized <T> T withPrivateKey(Function<byte[], T> action) {
        if (privateKeyLine == null) throw new IllegalStateException("signing key is sealed");
        lastUsed = clock.instant();
        return action.apply(privateKeyLine);
    }

    @Scheduled(fixedDelay = 60_000, initialDelay = 60_000)
    public synchronized void autoReseal() {
        if (privateKeyLine != null && lastUsed.plus(props.autoResealAfter()).isBefore(clock.instant())) {
            seal("system", null, "idle for " + props.autoResealAfter());
        }
    }

    static byte[] ascii(String s) {
        return s.getBytes(StandardCharsets.US_ASCII);
    }
}

package com.personaai.license.server.key;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;

import org.junit.jupiter.api.Test;

class SealedKeyFileTest {
    static final String PRIV = "stt-license-ed25519-private-v1:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";
    static final String PUB = "ebVWLo/mVPlAeLES6KmLp5AfhTrmlb7X4OORC60ElmQ=";
    static final char[] PASS = "a passphrase of sufficient length".toCharArray();

    SealedKeyFile sealed() {
        return SealedKeyFile.seal(PRIV.getBytes(StandardCharsets.US_ASCII), PASS.clone(), PUB, 100_000);
    }

    @Test
    void roundTripThroughJson() throws Exception {
        SealedKeyFile f = SealedKeyFile.parse(sealed().toJson());
        assertThat(new String(f.unseal(PASS.clone()), StandardCharsets.US_ASCII)).isEqualTo(PRIV);
        assertThat(f.publicKey()).isEqualTo(PUB);
        assertThat(f.publicKeyFingerprint()).hasSize(64);
        assertThat(f.toJson()).doesNotContain(PRIV.substring(31, 50)); // 평문 조각이 파일에 없음
    }

    @Test
    void wrongPassphraseFails() {
        assertThatThrownBy(() -> sealed().unseal("another passphrase of length".toCharArray()))
                .isInstanceOf(GeneralSecurityException.class);
    }

    @Test
    void headerTamperingIsDetectedByAad() {
        String json = sealed().toJson();
        // 공개키 필드만 다른 (유효한 32바이트) 값으로 바꿔치기
        String swapped = json.replace(PUB, "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8=");
        assertThatThrownBy(() -> SealedKeyFile.parse(swapped).unseal(PASS.clone()))
                .isInstanceOf(GeneralSecurityException.class);
        String created = json.replaceFirst("\"created_at\" : \"[^\"]+\"", "\"created_at\" : \"2000-01-01T00:00:00Z\"");
        assertThat(created).isNotEqualTo(json);
        assertThatThrownBy(() -> SealedKeyFile.parse(created).unseal(PASS.clone()))
                .isInstanceOf(GeneralSecurityException.class);
    }

    @Test
    void rejectsWeakParameters() {
        assertThatThrownBy(() -> SealedKeyFile.seal(PRIV.getBytes(), "short".toCharArray(), PUB, 100_000))
                .isInstanceOf(IllegalArgumentException.class);
        String low = sealed().toJson().replace("\"iterations\" : 100000", "\"iterations\" : 1000");
        assertThat(low).contains("1000");
        assertThatThrownBy(() -> SealedKeyFile.parse(low)).hasMessageContaining("iterations");
        assertThatThrownBy(() -> SealedKeyFile.parse("{\"format\":\"x\"}")).isInstanceOf(IllegalArgumentException.class);
    }
}

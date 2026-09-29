package com.personaai.license.server.security;

import static org.assertj.core.api.Assertions.assertThat;

import java.nio.charset.StandardCharsets;
import java.time.Instant;

import org.junit.jupiter.api.Test;

import com.personaai.license.server.crypto.Base32;

class TotpTest {
    static final byte[] SECRET = "12345678901234567890".getBytes(StandardCharsets.US_ASCII);

    /** RFC 6238 부록 B (SHA1, 8자리) */
    @Test
    void rfc6238Vectors() {
        assertThat(Totp.code(SECRET, Totp.step(Instant.ofEpochSecond(59)), 8)).isEqualTo("94287082");
        assertThat(Totp.code(SECRET, Totp.step(Instant.ofEpochSecond(1111111109)), 8)).isEqualTo("07081804");
        assertThat(Totp.code(SECRET, Totp.step(Instant.ofEpochSecond(1234567890)), 8)).isEqualTo("89005924");
        assertThat(Totp.code(SECRET, Totp.step(Instant.ofEpochSecond(2000000000)), 8)).isEqualTo("69279037");
        assertThat(Totp.code(SECRET, Totp.step(Instant.ofEpochSecond(20000000000L)), 8)).isEqualTo("65353130");
    }

    @Test
    void matchWindowAndFormat() {
        Instant now = Instant.ofEpochSecond(1234567890);
        long s = Totp.step(now);
        assertThat(Totp.match(SECRET, Totp.code(SECRET, s, 6), now)).isEqualTo(s);
        assertThat(Totp.match(SECRET, Totp.code(SECRET, s - 1, 6), now)).isEqualTo(s - 1);
        assertThat(Totp.match(SECRET, Totp.code(SECRET, s + 1, 6), now)).isEqualTo(s + 1);
        assertThat(Totp.match(SECRET, Totp.code(SECRET, s - 2, 6), now)).isEqualTo(-1);
        assertThat(Totp.match(SECRET, "12345", now)).isEqualTo(-1);
        assertThat(Totp.match(SECRET, "abcdef", now)).isEqualTo(-1);
        assertThat(Totp.match(SECRET, null, now)).isEqualTo(-1);
    }

    /** RFC 4648 §10 */
    @Test
    void base32Vectors() {
        assertThat(Base32.encode("".getBytes())).isEqualTo("");
        assertThat(Base32.encode("f".getBytes())).isEqualTo("MY");
        assertThat(Base32.encode("foobar".getBytes())).isEqualTo("MZXW6YTBOI");
        assertThat(new String(Base32.decode("MZXW6YTBOI======"))).isEqualTo("foobar");
        assertThat(new String(Base32.decode("mzxw 6ytb oi"))).isEqualTo("foobar");
    }
}

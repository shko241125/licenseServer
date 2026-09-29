package com.personaai.license.server.config;

import java.nio.file.Path;
import java.time.Duration;
import java.time.ZoneId;

import org.springframework.boot.context.properties.ConfigurationProperties;

@ConfigurationProperties("license")
public record LicenseProperties(
        Path licensectlPath,
        Path sealedKeyPath,
        String expectedPublicKeyFingerprint,
        Duration autoResealAfter,
        Path dataKeyFile,
        ZoneId zone,
        Duration sessionAbsoluteTimeout,
        Duration tempPasswordTtl,
        Bootstrap bootstrap,
        String defaultWarningNotice) {

    public record Bootstrap(String adminUsername, Path passwordFile) {}
}

package com.personaai.license.server;

import org.junit.jupiter.api.Tag;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.test.context.ActiveProfiles;
import org.springframework.test.context.DynamicPropertyRegistry;
import org.springframework.test.context.DynamicPropertySource;
import org.testcontainers.postgresql.PostgreSQLContainer;

/** 실제 PostgreSQL(컨테이너 1개를 모든 통합 테스트가 공유) + 실제 licensectl + 실제 봉인 키. */
@Tag("integration")
@SpringBootTest
@ActiveProfiles("test")
@org.springframework.context.annotation.Import(MutableClock.Config.class)
public abstract class IntegrationTestBase {
    protected static final PostgreSQLContainer PG = new PostgreSQLContainer("postgres:16-alpine")
            .withInitScript("db/test-roles.sql");
    protected static final TestFixture F = TestFixture.get();

    static {
        PG.start();
    }

    @DynamicPropertySource
    static void props(DynamicPropertyRegistry r) {
        r.add("spring.datasource.url", PG::getJdbcUrl);
        r.add("spring.datasource.username", () -> "license_app");
        r.add("spring.datasource.password", () -> "app-pw");
        r.add("spring.flyway.user", () -> "license_owner");
        r.add("spring.flyway.password", () -> "owner-pw");
        r.add("license.licensectl-path", TestFixture.LICENSECTL::toString);
        r.add("license.sealed-key-path", F.sealedKey::toString);
        r.add("license.data-key-file", F.dataKey::toString);
        r.add("license.bootstrap.password-file", F.bootstrapPassword::toString);
        r.add("license.bootstrap.admin-username", () -> "root.admin");
    }

    /** 소유자(마이그레이션) 계정 JDBC — 앱 권한 밖의 조작(변조 시뮬레이션)에 쓴다. */
    protected static java.sql.Connection ownerConnection() throws java.sql.SQLException {
        return java.sql.DriverManager.getConnection(PG.getJdbcUrl(), "license_owner", "owner-pw");
    }
}

import org.springframework.boot.gradle.plugin.SpringBootPlugin

plugins {
    java
    id("org.springframework.boot") version "4.1.1"
}

group = "com.personaai.license"
version = "1.0.0"

java {
    toolchain { languageVersion = JavaLanguageVersion.of(21) }
}

repositories { mavenCentral() }

dependencies {
    implementation(platform(SpringBootPlugin.BOM_COORDINATES))
    implementation("org.springframework.boot:spring-boot-starter-webmvc")
    implementation("org.springframework.boot:spring-boot-starter-thymeleaf")
    implementation("org.springframework.boot:spring-boot-starter-security")
    implementation("org.springframework.boot:spring-boot-starter-jdbc")
    implementation("org.springframework.boot:spring-boot-starter-flyway")
    implementation("org.springframework.boot:spring-boot-starter-validation")
    implementation("org.springframework.boot:spring-boot-starter-actuator")
    implementation("org.flywaydb:flyway-database-postgresql")
    runtimeOnly("org.postgresql:postgresql")

    testImplementation(platform(SpringBootPlugin.BOM_COORDINATES))
    testImplementation("org.springframework.boot:spring-boot-starter-test")
    testImplementation("org.springframework.boot:spring-boot-starter-webmvc-test")
    testImplementation("org.springframework.boot:spring-boot-starter-security-test")
    testImplementation("org.springframework.boot:spring-boot-testcontainers")
    testImplementation("org.testcontainers:testcontainers-postgresql")
    testImplementation("org.testcontainers:testcontainers-junit-jupiter")
    testRuntimeOnly("org.junit.platform:junit-platform-launcher")
}

// licensectl 경로: -Plicensectl=<path> (기본: 저장소 루트의 릴리스 빌드)
val licensectlPath = providers.gradleProperty("licensectl")
    .orElse(layout.projectDirectory.file("../build-release/licensectl").asFile.absolutePath)

tasks.withType<Test>().configureEach {
    systemProperty("licensectl.path", licensectlPath.get())
    jvmArgs("-XX:+EnableDynamicAgentLoading") // Mockito 인라인 에이전트 경고 방지
}

tasks.test {
    useJUnitPlatform { excludeTags("integration") }
}

// Docker(Testcontainers) 가 필요한 통합 테스트: ./gradlew integrationTest
val integrationTest by tasks.registering(Test::class) {
    description = "Integration tests (PostgreSQL via Testcontainers, real licensectl)"
    group = "verification"
    testClassesDirs = sourceSets.test.get().output.classesDirs
    classpath = sourceSets.test.get().runtimeClasspath
    useJUnitPlatform { includeTags("integration") }
    shouldRunAfter(tasks.test)
}

tasks.named<org.springframework.boot.gradle.tasks.bundling.BootJar>("bootJar") {
    archiveFileName = "license-server.jar"
}
tasks.named<Jar>("jar") { enabled = false }

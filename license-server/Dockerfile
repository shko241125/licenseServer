# 빌드: 저장소 루트에서  docker build -f license-server/Dockerfile -t license-server:<tag> .
# licensectl(C++) 과 서버를 같은 커밋에서 함께 빌드한다 — SDK 에 들어간 코어와 발급기가 같은 정규화 코드를 쓰도록.

# ---- 1. licensectl (런타임과 같은 Ubuntu 24.04 에서 빌드: glibc 일치, OpenSSL 정적 링크) ----
FROM ubuntu:24.04 AS native
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      g++ cmake ninja-build libssl-dev libgtest-dev ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY cmake cmake
COPY core core
COPY tools tools
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTT_LICENSE_OPENSSL_STATIC=ON \
 && cmake --build /build \
 && /build/stt_license_tests --gtest_brief=1 \
 && strip /build/licensectl \
 && ! ldd /build/licensectl | grep -q libcrypto

# ---- 2. 서버 jar (단위 테스트 포함; 통합 테스트는 Docker 가 필요해 CI/호스트에서 실행) ----
FROM eclipse-temurin:21-jdk-noble AS java
WORKDIR /src/license-server
COPY license-server/gradlew license-server/settings.gradle.kts license-server/build.gradle.kts ./
COPY license-server/gradle gradle
RUN ./gradlew --no-daemon -q dependencies > /dev/null
COPY license-server/src src
COPY --from=native /build/licensectl /opt/licensectl
RUN ./gradlew --no-daemon test bootJar -Plicensectl=/opt/licensectl

# ---- 3. 런타임 ----
FROM eclipse-temurin:21-jre-noble
RUN groupadd --system --gid 10001 license && useradd --system --uid 10001 --gid 10001 --no-create-home license \
 && mkdir -p /app /data/key /data/tls && chown -R root:root /app /data
COPY --from=native /build/licensectl /opt/licensectl
COPY --from=java /src/license-server/build/libs/license-server.jar /app/license-server.jar
USER 10001:10001
EXPOSE 8443
# 관리 포트(8081)는 컨테이너 안 127.0.0.1 에만 열린다
HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
  CMD curl -fsS http://127.0.0.1:8081/actuator/health | grep -q '"UP"' || exit 1
ENTRYPOINT ["java", "-XX:+DisableAttachMechanism", "-XX:+ExitOnOutOfMemoryError", "-XX:MaxRAMPercentage=70", \
            "-Djava.io.tmpdir=/tmp", "-jar", "/app/license-server.jar"]

# 사내 라이선스 발급 서버 (license-server)

내부망 전용 웹 서버입니다. 관리자가 로그인해 계약 정보를 입력하면, 저장소의 `licensectl`(C++ 코어와 같은 정규화·서명 코드)로 서명한 `license.lic`를 발급합니다.

- 스택: Spring Boot 4.1.1 · Java 21 · Spring Security 7 · Thymeleaf · PostgreSQL 16 · Flyway · Gradle 9.8
- 구성: `web`(이 서버) + `db`(PostgreSQL) 두 컨테이너

## 1. 보안 구조 요약

| 항목 | 방식 |
|---|---|
| 서명 비밀키 | 디스크에는 **봉인 파일**만 둡니다(`PBKDF2-HMAC-SHA256` 600,000회 + `AES-256-GCM`, 헤더 전체를 AAD로 묶음). 기동 후 관리자가 해제(unseal)하면 메모리에만 있고, 8시간 동안 발급이 없으면 자동으로 다시 봉인합니다. `licensectl`에는 stdin으로만 전달합니다 |
| 서명 | `licensectl sign`을 하위 프로세스로 호출합니다. 셸을 거치지 않고, 환경변수를 비우고, 10초 타임아웃과 256 KiB 출력 상한을 둡니다. 발급 직후 공개키로 다시 검증합니다 |
| 로그인 | 개인 계정, BCrypt(12), 12자 이상 비밀번호, 최근 3개 재사용 금지. 5회 실패 시 15분 잠금, IP당 5분 20회 제한 |
| 임시 비밀번호 | 관리자가 계정 생성·초기화 때 정한 비밀번호는 **72시간 뒤 만료**(`license.temp-password-ttl`, 환경변수 `LICENSE_TEMP_PASSWORD_TTL`). 초기 관리자 비밀번호는 만료 없음 |
| 2단계 인증 | TOTP(RFC 6238)는 **필수**입니다. 같은 코드는 한 번만 쓸 수 있습니다. 비밀값은 데이터 키로 AES-GCM 암호화해 저장합니다 |
| 로그인 단계 | 비밀번호 → (첫 로그인이면 비밀번호 변경) → (OTP 미등록이면 등록, 아니면 OTP 확인) → 전체 권한. 단계가 오를 때마다 세션 ID를 새로 발급합니다 |
| 민감 작업 | 키 해제, 무효 처리, 계정 관리는 비밀번호와 OTP를 다시 확인합니다 |
| 역할 | `ADMIN` > `ISSUER` > `VIEWER` |
| 세션 | 유휴 15분, 절대 8시간, 계정당 1개. `__Host-LSID; Secure; HttpOnly; SameSite=Strict` |
| 웹 | CSRF, CSP(`script-src 'self'`, 인라인 금지), `frame-ancestors 'none'`, HSTS, no-referrer, no-store |
| 감사 로그 | 앱 DB 계정은 **INSERT·SELECT만** 할 수 있고, 행은 **SHA-256 해시 체인**으로 이어집니다. 백업할 때 마지막 해시를 앵커로 DB 밖에 기록합니다 |
| 라이선스 기록 | 상태 전이는 트리거로 강제합니다(PENDING→ISSUED/FAILED, ISSUED→VOID(사유 필수)). 계약 필드는 바꿀 수 없고, DELETE 권한도 없습니다 |
| 컨테이너 | uid 10001, 읽기 전용 루트, `cap_drop: ALL`, `no-new-privileges`, 스왑 없음. DB는 외부 통신이 없고, web은 외부로 나가는 NAT가 꺼져 있습니다 |

## 2. 빌드

인터넷이 되는 빌드 머신에서 **저장소 루트**를 빌드 컨텍스트로 씁니다.

```bash
docker build -f license-server/Dockerfile -t license-server:1.0.0 .
```

- 빌드 중에 C++ 코어 테스트(38개)와 서버 단위 테스트(39개)가 실행됩니다. 하나라도 실패하면 이미지가 만들어지지 않습니다.
- 인터넷이 없는 내부망으로 옮길 때:
  ```bash
  docker save license-server:1.0.0 postgres:16-alpine | gzip > license-server-1.0.0.tar.gz
  sha256sum license-server-1.0.0.tar.gz > license-server-1.0.0.tar.gz.sha256   # 함께 반입해 대조
  # 내부망 호스트에서
  sha256sum -c license-server-1.0.0.tar.gz.sha256 && gunzip -c license-server-1.0.0.tar.gz | docker load
  ```

개발 중 테스트:
```bash
cmake -S .. -B ../build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTT_LICENSE_BUILD_TESTS=OFF \
      -DSTT_LICENSE_OPENSSL_STATIC=ON && cmake --build ../build-release --target licensectl
./gradlew test                 # 단위 39개 (licensectl 필요)
./gradlew integrationTest      # 통합 17개 (Docker 필요: Testcontainers PostgreSQL)
```

## 3. 설치

**리눅스 파일시스템**에서 진행합니다. WSL의 `/mnt/c` 같은 곳은 파일 권한이 적용되지 않아 비밀값이 보호되지 않습니다.

```bash
cp -r license-server/deploy /opt/license-server && cd /opt/license-server
./init-secrets.sh license.example.internal     # DB 비밀번호, 데이터 키, 초기 관리자 비밀번호, 임시 TLS 인증서
```
- `secrets/`(0700)에 파일이 생깁니다. 파일 자체는 컨테이너 사용자가 읽을 수 있도록 0444입니다.
- 운영 TLS는 사내 CA 인증서로 바꿉니다. `secrets/tls_keystore.p12`(PKCS#12)와 `secrets/tls_keystore_password`를 교체하면 됩니다.

### 키 생성식 (최초 1회)
네트워크 없는 컨테이너에서 **운영자 uid로** 실행해야, 생성된 파일을 운영자가 다룰 수 있습니다.
```bash
mkdir -m 700 ceremony && U="$(id -u):$(id -g)"
docker run --rm --network none --user $U --entrypoint /opt/licensectl -v $PWD/ceremony:/work \
  license-server:1.0.0 keygen -out /work/keys
docker run --rm -it --network none --user $U --entrypoint java -v $PWD/ceremony:/work \
  license-server:1.0.0 -jar /app/license-server.jar seal-key \
  --in /work/keys/private.key --out /work/sealed-key.json --licensectl /opt/licensectl
#  → 패스프레이즈(20자 이상)를 두 번 입력. 출력된 fingerprint 를 기록
install -m 444 ceremony/sealed-key.json data/key/sealed-key.json
cp ceremony/keys/public.key ./public.key           # SDK 빌드 담당자에게 전달 (docs/sdk-cmake.md)
shred -u ceremony/keys/private.key                  # 평문 비밀키 삭제
echo "LICENSE_PUBLIC_KEY_FP=<fingerprint>" > .env   # 봉인 파일 바꿔치기 방지용 지문 고정
echo "LICENSE_BIND_IP=<관리망 IP>" >> .env
echo "LICENSE_SERVER_TAG=1.0.0" >> .env
```
- 패스프레이즈는 두 명이 나눠 봉인 보관합니다. `sealed-key.json`은 오프라인 매체에 2부 백업합니다.
- 패스프레이즈를 잃어버리면 새 키를 만들고 SDK를 다시 배포해야 합니다.

### 기동
```bash
docker compose up -d
docker compose ps        # web: healthy
```
- `https://<관리망 IP>:8443`에서 `admin`과 `secrets/initial_admin_password`로 로그인합니다.
- 첫 로그인 때 비밀번호를 바꾸고 OTP를 등록합니다. 이후 개인 계정을 만들고, 공용 `admin` 사용은 줄입니다.

### 네트워크 권장 설정
- **감사 로그의 접속 IP:** Docker의 userland-proxy가 켜져 있으면 모든 접속이 게이트웨이 IP(예: `172.x.0.1`)로 기록됩니다. 실제 관리자 PC의 IP를 남기려면 `/etc/docker/daemon.json`에 `{"userland-proxy": false}`를 넣고 Docker를 재시작합니다.
- **외부 송신 차단 강화:** compose는 web 네트워크의 NAT를 꺼서 외부 연결이 응답을 받지 못하게 합니다(검증함). 사설 IP 패킷 자체까지 막으려면 호스트 방화벽에 규칙을 추가합니다.
  ```bash
  iptables -I DOCKER-USER -s <edge 네트워크 대역> -m conntrack --ctstate NEW -j DROP
  ```

## 4. 운영

| 작업 | 방법 |
|---|---|
| 발급 | 키 → 해제(관리자) → 발급 → 미리보기 확인 → 발급 → `license.lic` 다운로드. 끝나면 "지금 봉인" |
| 갱신 | 상세 화면 → "이 건으로 갱신 발급"(원본 종료 다음 날부터 1년) |
| 고객 문의 | 검증 화면에 파일을 올리면 서명·상태·만료일을 보여 줍니다 |
| 백업 | `./backup.sh` (cron 매일). DB 덤프(0600)와 `audit-anchor.log`(마지막 감사 해시). 앵커 파일은 다른 곳에도 보관 |
| 감사 무결성 | 화면 "해시 체인 무결성 검사"(주 1회) + `./verify-anchors.sh [외부 보관 앵커]`(월 1회). 방법론: `docs/audit-hash-chain.md` |
| 복원 | `docker compose stop web` → `docker compose exec -T db pg_restore -U postgres -d license --clean < backups/license-<시각>.dump` → `docker compose start web` 후 감사 로그 화면에서 "해시 체인 무결성 검사". 결과의 마지막 해시가 `audit-anchor.log`와 같은지 확인. **분기마다 훈련** |
| 계정 | 계정 관리 화면에서 추가·비활성화·비밀번호 초기화·OTP 초기화. 비활성화하면 그 사용자의 세션이 즉시 끝납니다 |
| 재시작 | 재시작하면 키가 봉인 상태로 돌아갑니다. 관리자가 다시 해제해야 합니다 |
| 헬스 | 컨테이너 안 `127.0.0.1:8081/actuator/health`(Docker HEALTHCHECK). 외부 포트에는 노출되지 않습니다 |

## 5. 문제 해결

| 증상 | 원인·조치 |
|---|---|
| 기동 로그 `cannot read data key file` | `secrets/data_key`가 없음 → `init-secrets.sh` |
| 키 화면 `MISSING` · `fingerprint does not match` | 봉인 파일이 없거나 `.env`의 `LICENSE_PUBLIC_KEY_FP`와 다름. 파일 바꿔치기 가능성을 확인 |
| 해제 `KEY_MISMATCH` | 봉인 파일 안의 비밀키와 공개키 필드가 짝이 맞지 않음(파일 손상·조작) |
| 해제 "재인증에 실패" | 비밀번호나 OTP가 틀림, 또는 **같은 30초 구간의 OTP 재사용**(다음 코드를 기다려 입력) |
| 해제 `LOCKED` | 패스프레이즈 5회 오류 → 15분 뒤 재시도 |
| 로그인 "임시 비밀번호가 만료되었습니다" | 관리자가 계정 관리에서 비밀번호를 다시 초기화(새 임시 비밀번호, 새 만료 시각) |
| 브라우저 인증서 경고 | 자체 서명 인증서 → `secrets/tls.crt`를 관리자 PC에 신뢰 등록하거나 사내 CA 인증서 사용 |
| `seal-key`가 파일을 못 씀 / 운영자가 봉인 파일을 못 읽음 | 키 생성식 컨테이너를 `--user $(id -u):$(id -g)`로 실행 |

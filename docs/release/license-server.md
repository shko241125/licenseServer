# 발급 서버 배포 (license-server)

사내 라이선스 발급 서버를 실물 서버(인터넷 없는 내부망)에 설치·업그레이드하기 위한 배포 방식과 방법, 배포 파일을 정리합니다. SDK 배포는 원 저장소의 `docs/release/sdk.md`를 봅니다(이 패키지에는 없음).
- 이 문서는 발급 서버 패키지에도 들어갑니다(`docs/release/license-server.md`).
- 설치 후 운영(발급·백업·복원·감사)은 [../../license-server/README.md](../../license-server/README.md)를 봅니다.

## 1. 개요

| 항목 | 내용 |
|---|---|
| 산출물 | `license-server-<버전>.tar.gz` + `license-server-<버전>.tar.gz.sha256` |
| 받는 사람 | 발급 서버 운영자(설치·업그레이드 담당) |
| 내용 | Docker 이미지 2개(`license-server:<버전>`, `postgres:16-alpine`), 배포 구성(compose·스크립트·DB 초기화), 설치·운영 문서 |
| 보내지 않는 것 | 비밀값(`secrets/`), `.env`, 봉인 키(`data/key/`), DB 데이터·백업, 서명 비밀키. 모두 **설치하는 서버에서 만듭니다** |
| 버전 원본 | `license-server/build.gradle.kts`의 `version = "x.y.z"` |
| 태그 | `server-v<버전>` (주석 태그) |
| 생성 도구 | `scripts/make-server-release.sh`, 담을 목록 `scripts/server-release-files.txt` |

버전 규칙:
- x: 설치·업그레이드에 수동 조치가 필요한 변경(비밀값 형식, compose 구조, 키 봉인 형식).
- y: 기능 추가, DB 마이그레이션 추가.
- z: 수정만.

## 2. 배포 방식

| 방식 | 내용 | 이유 |
|---|---|---|
| 이미지 동봉 | 빌드한 이미지를 `docker save`로 패키지에 넣습니다. 설치 서버는 `docker load`만 합니다 | 설치 서버에 인터넷이 없음. 설치 서버에서 빌드하지 않음 |
| 커밋 기준 빌드 | 태그 커밋의 소스(`git archive`)로 이미지를 빌드합니다. 이미지에 버전·커밋 라벨(`org.opencontainers.image.version`/`revision`)을 붙입니다 | 이미지가 어느 커밋인지 증명. 빌드 중 C++ 코어·서버 단위 테스트가 실패하면 이미지가 만들어지지 않음 |
| 같은 커밋의 서명 도구 | 이미지 안 `licensectl`은 같은 커밋의 C++ 코어로 빌드됩니다 | SDK에 들어간 검증기와 같은 정규화·서명 코드(D20) |
| 허용 목록 | 문서·배포 구성은 목록에 적힌 경로만 담습니다 | 비밀값·내부 자료가 섞이지 않게 |
| 비밀값은 현장 생성 | `init-secrets.sh`와 키 생성식을 설치 서버에서 실행합니다 | 패키지가 유출돼도 비밀이 없음 |
| 기동 시험 | `--verify`는 풀어낸 패키지만으로 새 스택을 설치·기동해 healthy·로그인 화면·DB 마이그레이션을 확인하고 지웁니다 | 문서대로 설치되는지 패키지 단위로 증명 |
| 재현성 한계 | 패키지 묶음(순서·소유자·시각)은 고정하지만, 이미지는 빌드할 때마다 바이트가 달라집니다(패키지 설치 시각 등) | 증명은 바이트 비교가 아니라 이미지 라벨의 커밋 + `SHA256SUMS` + 발행 기록의 해시로 함 |

## 3. 배포 방법

### 발행하는 쪽 (발행 담당)
1. 버전을 정합니다. `license-server/build.gradle.kts`의 `version`과 아래 §5 발행 이력을 함께 고칩니다.
2. 통합 테스트를 통과시킵니다(Docker 필요).
   ```bash
   cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTT_LICENSE_BUILD_TESTS=OFF \
     -DSTT_LICENSE_OPENSSL_STATIC=ON && cmake --build build-release --target licensectl
   (cd license-server && ./gradlew test integrationTest)
   ```
3. 커밋하고 태그를 답니다: `git tag -a server-v<버전> -m "license-server <버전>"`
4. 패키지를 만들고 검증합니다(리눅스, Docker, 인터넷: 빌드 중 의존성 내려받기).
   ```bash
   scripts/make-server-release.sh --ref server-v<버전> --verify
   # → build/server-release/license-server-<버전>.tar.gz (+ .sha256), 마지막에 "verified: ..."
   ```
   커밋 전 사전 점검은 `--worktree`로 합니다. 버전에 `-dirty`가 붙으며 발행하지 않습니다.
5. §6 수용 기준을 확인하고 태그를 원격에 올립니다.
6. `.tar.gz`와 `.sha256`을 운영자에게 전달합니다(내부망 반입 매체). `.sha256`은 가능하면 다른 경로로도 알립니다.
7. `docs/dev/STATUS.md`에 발행 기록(커밋·해시·검증)을 남깁니다.

### 받는 쪽 — 새로 설치
```bash
sha256sum -c license-server-<버전>.tar.gz.sha256
tar -xzf license-server-<버전>.tar.gz && cd license-server-<버전>
sha256sum --quiet -c SHA256SUMS && cat VERSION
docker load -i images/license-server-<버전>.tar
docker load -i images/postgres-16-alpine.tar
sudo install -d -o "$USER" -m 755 /opt/license-server
cp -r license-server/deploy/. /opt/license-server/ && cd /opt/license-server
```
이어서 [license-server/README.md](../../license-server/README.md) §3(비밀값 생성 → 키 생성식 → 기동)을 따릅니다. `.env`의 `LICENSE_SERVER_TAG`는 이 패키지의 버전입니다.

### 받는 쪽 — 업그레이드
1. **백업**: `./backup.sh` (DB 덤프와 감사 앵커). 덤프 파일 이름을 적어 둡니다.
2. 이전 이미지 버전(`.env`의 `LICENSE_SERVER_TAG`)을 적어 둡니다. 이미지는 지우지 않습니다.
3. 새 패키지를 대조하고 이미지를 `docker load` 합니다(위와 같음).
4. 배포 구성을 덮어씁니다: `cp -r license-server/deploy/. /opt/license-server/`
   - 패키지에 `secrets/`·`data/`·`.env`·`backups/`가 없으므로 기존 비밀값·봉인 키·백업은 그대로입니다.
5. `.env`의 `LICENSE_SERVER_TAG`를 새 버전으로 바꾸고 `docker compose up -d --wait`.
   - DB 마이그레이션(Flyway)이 기동 때 자동 적용됩니다. 로그 `Successfully applied N migrations`.
6. 확인: `docker compose ps`(healthy), 로그인, 키 해제, 감사 로그 "해시 체인 무결성 검사".

**되돌리기:** 새 버전이 DB 마이그레이션을 적용했다면 이전 이미지만 다시 띄워서는 안 됩니다(이전 이미지는 새 스키마를 모름). `docker compose stop web` → 1단계 덤프로 복원(README §4 "복원") → `.env`를 이전 버전으로 → `docker compose up -d`.

## 4. 배포 파일 목록

### 전달 파일
| 파일 | 내용 |
|---|---|
| `license-server-<버전>.tar.gz` | 패키지. 최상위 디렉터리 `license-server-<버전>/` |
| `license-server-<버전>.tar.gz.sha256` | 위 파일의 SHA-256 (`sha256sum -c` 형식) |

### 패키지 안
| 경로 | 내용 |
|---|---|
| `README.md`, `VERSION`, `SHA256SUMS` *(생성)* | 설치 첫걸음 안내, 버전·원 커밋·이미지 ID, 파일 해시 |
| `images/license-server-<버전>.tar` *(생성)* | 발급 서버 이미지(`docker save`). 서버 jar + `licensectl` |
| `images/postgres-16-alpine.tar` *(생성)* | DB 이미지(`docker save`). 다이제스트는 `VERSION`에 기록 |
| `license-server/deploy/docker-compose.yml` | web·db 구성(읽기 전용 루트, 내부 DB 네트워크, 비밀값 파일) |
| `license-server/deploy/init-secrets.sh` | 비밀값·임시 TLS 인증서 생성(최초 1회) |
| `license-server/deploy/backup.sh`, `verify-anchors.sh` | 일일 백업·감사 앵커 대조 |
| `license-server/deploy/db-init/01-roles.sh` | DB 최초 초기화 때 계정 분리 |
| `license-server/README.md` | 보안 구조, 설치, 운영, 문제 해결 |
| `license-server/docs/audit-hash-chain.md` | 감사 로그 해시 체인 방법론 |
| `license-server/docs/licensectl-operations.md` | 발급 서버 없이 `licensectl`로 발급할 때의 절차 |
| `docs/release/license-server.md` | 이 문서 |
| `docs/sdk/` | 서버 문서가 가리키는 SDK 문서 묶음(공개키 전달 01, 라이선스 규격 04, 현장 조치 06 등. 서로 링크하므로 전체) |

정확한 목록은 패키지의 `SHA256SUMS`가 원본입니다.

### 패키지에 절대 들어가면 안 되는 것 (스크립트가 검사)
| 대상 | 검사 |
|---|---|
| `secrets/`, `data/`, `backups/`, `.env`, `private.key`, `sealed-key.json` | 파일 이름 |
| `*.key`, `*.lic`, `*.dump`, `*.pdf`, `*.swp`, `work_*` | 파일 이름 |
| 비밀키 값(`stt-license-ed25519-private-v1:` + Base64), PEM 비밀키 | 문서·구성 파일 내용, 이미지 안 jar 내용과 `licensectl` |
| 이미지 안 jar의 `*.key`·`*.lic`·`*.env` 항목 | jar 목록 |
| 문서의 상대 링크가 패키지 밖을 가리킴 | 링크 검사 |

## 5. 발행 이력

| 버전 | 태그 → 커밋 | 패키지 SHA-256 | 비고 |
|---|---|---|---|
| 1.7.0 | `server-v1.7.0` → `6b9fc33` | `783f6be627e8d0fb92fb0fd304951a70a0f461069c6736fe5fb179e5ff8b67b4` | 첫 정식 발행. DB 마이그레이션 V1~V6, 라이선스 형식 `license_ver` 1, 개발 총 개발기한·1회 발급 상한(D43) |

## 6. 발행 수용 기준

- [ ] `./gradlew test integrationTest` 통과(태그 커밋)
- [ ] `make-server-release.sh --ref <태그> --verify` 성공(패키지만으로 설치·기동, `verified: ...`)
- [ ] `VERSION`의 커밋이 태그와 같고, 이미지 라벨 `org.opencontainers.image.revision`도 같음
- [ ] 이번 버전의 DB 마이그레이션 목록과 되돌리기 영향(복원 필요 여부)이 발행 기록에 있음
- [ ] 이미 적용된 마이그레이션 파일(`V*.sql`)이 바뀌지 않음(Flyway 체크섬)

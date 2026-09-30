# licenseServer — 작업 세션 안내

폐쇄망 STT 제품용 오프라인 라이선스 시스템입니다.
- 사내에서 Ed25519로 서명한 평문 JSON 라이선스를 STT SDK(C++/JNI)가 내장 공개키로 검증합니다.
- 이 저장소에는 두 가지가 들어 있습니다.
  - 검증 코어·SDK 적용 자료: 별도 패키지로 발행
  - 사내 발급 서버: `license-server/`

## 1. 먼저 읽을 것 (순서대로)
1. [docs/dev/STATUS.md](docs/dev/STATUS.md) — 지금 상태, 다음 작업, 미결 사항, 로컬에서만 검증 가능한 항목
2. [docs/dev/DECISIONS.md](docs/dev/DECISIONS.md) — 결정과 이유. **이미 결정된 것을 다시 논쟁하지 않는다**
3. 작업 대상의 문서:
   - SDK: [docs/sdk/README.md](docs/sdk/README.md)
   - 발급 서버: [license-server/README.md](license-server/README.md)

`git log -1 -- docs/dev/STATUS.md`와 `git log -1`을 비교합니다. STATUS를 갱신한 뒤에 커밋이 더 있으면, STATUS를 믿기 전에 그 커밋들을 확인합니다.

## 2. 구성
| 경로 | 내용 |
|---|---|
| `core/` | `stt_license` C++17 라이브러리(공개 헤더는 C++11). 엄격 JSON, RFC 8785 정규화, Ed25519(OpenSSL EVP), 검증 API, `Manager` |
| `tools/licensectl/` | CLI: keygen / issue / verify / sign / pubkey. 발급 서버가 하위 프로세스로 호출 |
| `integration/` | JNI 참조 구현 + Java 결합 테스트(`harness/`), SDK 형태 CMake 소비자(`cmake-consumer/`) |
| `license-server/` | Spring Boot 4.1 / Java 21 / Gradle 발급 서버, PostgreSQL, Docker 배포 |
| `docs/sdk/` | SDK 적용 문서(발행 대상), `docs/dev/` 작업 상태·결정 |
| `scripts/` | `check.sh`(전체 검증), `make-sdk-release.sh`(SDK 패키지), `cloud-setup.sh`(도구 준비) |

## 3. 검증 명령
```bash
scripts/cloud-setup.sh                            # 새 환경: 필요한 도구가 없을 때만 설치 (멱등)
scripts/check.sh                                  # C++ 코어·CLI·JNI·공격 실험·CMake 소비자 7단계 → "ALL CHECKS PASSED"
# 발급 서버 테스트는 단위·통합 모두 실제 licensectl(../build-release/licensectl)을 쓴다 → 먼저 빌드
# (없으면 ExceptionInInitializerError, 원인 메시지 "licensectl not found at ...")
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTT_LICENSE_BUILD_TESTS=OFF \
  -DSTT_LICENSE_OPENSSL_STATIC=ON && cmake --build build-release --target licensectl
(cd license-server && ./gradlew test)             # 단위 테스트
(cd license-server && ./gradlew integrationTest)  # 통합 테스트 (Docker/Testcontainers 필요)
scripts/make-sdk-release.sh --worktree --verify   # SDK 패키지를 풀어 단독 검증 (발행 전)
```
- 변경한 부분에 해당하는 검증은 반드시 실행하고, 결과(통과 수·실패 출력)를 사실대로 보고합니다.
- WSL의 `/mnt/c` 아래에서는 권한 검사 일부를 건너뜁니다. 리눅스 파일시스템에서 돌리는 것이 정확합니다.
- 컨테이너 안에서 호스트 Docker 소켓으로 통합 테스트를 돌리면 `Could not connect to Ryuk`가 날 수 있습니다. 이때는 `--network host`와 `TESTCONTAINERS_HOST_OVERRIDE=localhost`를 씁니다.

## 4. 절대 규칙
**이 저장소는 공개(public)입니다.** 커밋하는 모든 것이 공개된다고 가정합니다.

| 등급 | 예 | 위치 |
|---|---|---|
| 커밋 가능 | 코드, SDK 문서 수준의 설계, 결정과 이유, 진행 상태, 검증 결과 수치 | 저장소 |
| 로컬 전용 | 제품 이미지 역분석 세부, 공격 재현 절차 세부, 개발 스택 계정, 요구사항 원문 PDF | `work_claude/`(git 무시) |
| 금지 | 비밀키, 비밀번호·OTP 시드, `.env`, `license-server/deploy/secrets/`, 고객 라이선스·기존 `.key` 실물 | 어디에도 |

- 커밋 전 검사(출력이 없어야 함):
  ```bash
  git diff --cached | grep -nE 'stt-license-ed25519-private-v1:[A-Za-z0-9+/]{43}=|BEGIN [A-Z ]*PRIVATE KEY'
  git diff --cached --name-only | grep -E '(^|/)(private\.key|\.env)$|\.pdf$|deploy/secrets/|\.swp$'
  ```
- 개발 키와 라이선스는 `build/` 아래에 만들고 커밋하지 않습니다. 테스트는 키를 매번 새로 만듭니다.
- `core/testdata/golden_*`은 정규화 호환성의 증거입니다. 바꾸면 배포된 라이선스가 깨질 수 있으므로, 사용자 승인 없이 재생성하지 않습니다.
- `Error` 열거형 순번(오류 코드 `1000 + 순번`)은 고정입니다. 끝에만 추가할 수 있습니다.
- 사용자의 로컬 개발 배포 스택(계정·OTP·DB)을 초기화하거나 건드리지 않습니다.

## 5. 작업 방식
- 계획을 먼저 세우고, 중요한 중간 결과는 사용자가 검토할 수 있게 멈춥니다.
- 추측을 사실처럼 쓰지 않습니다. 실측하거나 공식 문서로 확인하고, 틀린 계획은 정정 사유와 함께 기록합니다.
- 부작용 없이 작업합니다. 공유 상태를 바꾸는 작업(푸시, 원격 설정, 삭제)은 사용자가 요청할 때만 합니다.
- **작업 단위를 끝낼 때**:
  1. 검증을 실행합니다.
  2. `docs/dev/STATUS.md`를 갱신하고, 새 결정이 있으면 `docs/dev/DECISIONS.md`에 추가합니다(기존 항목은 고치지 않고 "대체됨" 표시만).
  3. 같은 커밋에 포함합니다.
- 커밋은 사용자가 요청할 때만 합니다.
  - 메시지 형식: `type(scope): 한국어 요약`(feat, fix, test, docs, build)
  - 본문에는 무엇을 왜 바꿨는지 적습니다.
  - 주제별로 나눠 커밋합니다.
- 브랜치: 작업마다 만듭니다. 로컬과 클라우드 세션이 같은 브랜치를 동시에 고치지 않습니다.

## 6. 응답 규칙 (저장소 소유자의 개인 작업 규칙)
- 모든 응답과 문서는 **한국어**로 씁니다. 코드 식별자와 기술 용어는 원문 그대로 둡니다.
- 응답 끝에 `## 📚 학습 노트`를 둡니다. 내용이 없으면 생략합니다.
  - 처음 접할 만한 개념·도구·분기점을 1~5개 고릅니다.
  - 항목마다 2~4줄: `**용어** — 정의 → 이 작업에서 쓰인 이유 → (필요하면) 비유`
  - 비유가 어디서 깨지는지 한 줄, 근거가 필요하면 출처, "더 깊이 볼 것" 한 줄을 붙입니다.
  - 이미 다룬 내용은 다시 넣지 않습니다.

## 7. 클라우드 세션 (claude.ai/code)
- 원격 세션에서는 `CLAUDE_CODE_REMOTE=true`입니다.
- 환경의 setup script로 `bash scripts/cloud-setup.sh`를 등록해 둡니다.
- 사용자 로컬 설정(`~/.claude/CLAUDE.md`, 자동 메모리)은 로드되지 않습니다. 필요한 규칙은 모두 이 파일에 있습니다.
- `work_claude/`(상세 계획·보고서 원문)는 클라우드에 없습니다. 상태와 결정의 원본은 `docs/dev/`입니다.
- 클라우드에서 재현할 수 없는 검증은 STATUS.md의 "로컬 전용 검증" 목록을 봅니다. 클라우드 결과만으로 그 항목을 완료로 표시하지 않습니다.
- 작업 결과는 **푸시해야** 다른 세션에 전달됩니다.

# 작업 상태 (STATUS)

- **갱신:** 2026-09-30
- **기준:** 이 파일을 갱신한 커밋의 트리(`git log -1 -- docs/dev/STATUS.md`). 이후 커밋이 더 있으면 그 변경을 먼저 확인합니다.
- 결정과 이유는 [DECISIONS.md](DECISIONS.md), 작업 규칙은 [../../CLAUDE.md](../../CLAUDE.md)를 봅니다.

## 1. 구성 요소별 상태

| 구성 요소 | 상태 | 비고 |
|---|---|---|
| 검증 코어 `stt_license` 1.1.2 | 완료 | 서명 전용 API, `VerifyMode`, `Utf16ToUtf8`, `Manager`. 공개 헤더 C++11. 1.1.2: 공개키 경로 결정 규칙(D29) — **1.1.1은 상대 `-D` 회귀가 있어 쓰지 않음** |
| `licensectl` | 완료 | keygen / issue / verify / sign / pubkey |
| JNI 참조 구현·하네스 | 완료 | `connect(callback, configFile, hostLicense)` 형태, 모드 선택, 기존 `.key` 형태 보존 검증 |
| SDK 적용 문서·발행 | 완료 | `docs/sdk/` 01~06, `scripts/make-sdk-release.sh`(허용 목록) |
| 발급 서버 v1.2 | 완료 | 로그인·TOTP·봉인 키·발급·감사 해시 체인·임시 비밀번호 만료. Docker 배포. v1.2: 화면 전면 정리(사이드바 앱 셸, 디자인 토큰, 한국어 상태 배지, 폼 묶음·칸별 오류, 위험 작업 구분), 새 발급 폼 오류 시 "갱신 발급" 오표시 수정 |
| 원격 작업 기반 | 완료(클라우드 실측 전) | `CLAUDE.md`, 이 파일, `DECISIONS.md`, `scripts/cloud-setup.sh` |
| SDK·STT 서버 실제 적용 | **미착수(다른 저장소)** | 계획: `docs/sdk/02-integration.md`, `03-stt-server.md`, 수용 기준 `05-testing.md` §3 |

## 2. 마지막 검증

| 날짜 | 환경 | 항목 | 결과 |
|---|---|---|---|
| 2026-09-30 | 빈 Ubuntu 24.04 컨테이너, 커밋 대상 파일만(클라우드 모사) | `scripts/cloud-setup.sh` 1회차 / 2회차 | 설치 후 성공 134초 / 설치 없이 11초(멱등) |
| 〃 | 〃 | `scripts/check.sh` | `ALL CHECKS PASSED`: C++ 단위 49, JNI 결합 47항목, JNI export `Java_*` 10개만, LD_PRELOAD 차단·대조군 2종 우회, CMake 소비자 4구성 + 키 누락·비밀키 거부 |
| 〃 | 〃 | `./gradlew test` | 39개 통과 |
| 〃 | 〃 (`--network host`, `TESTCONTAINERS_HOST_OVERRIDE=localhost`) | `./gradlew integrationTest` | 20개 통과 |
| 〃 | Ubuntu 20.04 컨테이너(CMake 3.16, 헤드리스 JDK 17) | JNI 하네스 빌드 | 성공(`JAVA_HOME` 지정 필요) |
| 〃 | 로컬 WSL | `scripts/make-sdk-release.sh --worktree --verify` (1.1.2) | 풀어낸 SDK 패키지만으로 `check.sh` 통과. 키 경로 회귀 시험 4종(`normalvar`, `pinned+D`, `relative-D`, `relative-normal`) 포함 |
| 2026-10-02 | 분리된 미리보기 스택(웹 이미지만 교체, 같은 DB) | 발급 서버 UI 전후 비교 | 화면 37장씩 전 흐름 수행, 콘솔 오류 1→0, 가로 넘침 0(1440·390px), `gradlew test` 39·`integrationTest` 20 통과, 이미지 빌드 성공 |
| 〃 | Ubuntu 20.04·24.04 컨테이너, 로컬 | 공개키 주입 방식 행렬 9종(CMake 3.16·3.28·4.0, 라이브러리 디렉터리에 함정 키 배치) | 세 버전 동일: 절대·상대 `-D` → 지정 키, 일반 변수(+`-D`) → 지정 키 고정, 일반 변수 상대 경로 → 거부, CACHE+`-D` → `-D` 키, FORCE → 고정, 환경 변수 브리지 → 지정 키. 함정 키 내장 0건 |

## 3. 다음 작업 (우선순위)

1. **[사용자]** 저장소 가시성 결정(현재 **공개**). 공개 이력에 남은 내용 처리 여부 포함 — D26
2. **[사용자]** claude.ai/code 환경 생성(GitHub 접근, 네트워크 Trusted, setup script `bash scripts/cloud-setup.sh`, 비밀 없음) → §6 시범 운행
3. **[결정 대기]** 복제 대책 C2 채택 여부(D24), 라이선스 갱신 방식(D18)
4. **[SDK 저장소]** 적용 S1~S6: CMake 결합(공개키는 `docs/sdk/01-build.md` §6.3 방식), 심볼 은닉, `connect(hostLicense)`, 기존 `.key` 공존, 예약 타임아웃, 서버 오류 처리
5. **[이 저장소]** CI 구성(`check.sh`, Gradle 두 작업. 공개 저장소면 GitHub Actions 무료)
6. **[완료 2026-10-01]** 로컬 PC의 Google Drive 미러 동기화가 만든 옛 사본 6개를 삭제했다(git 이력 일치 확인 후).
   - 재발 방지: 로컬 작업 폴더의 git 데이터를 동기화 폴더 밖으로 분리했다(D31).
   - 클라우드 세션과는 무관하다.
7. **[완료 2026-10-02]** 로컬 개발 스택을 영구 위치에 새로 구성했다(프로젝트 `license-dev`, 포트 8443, 새 UI 이미지).
   - 이전 스택(`license-server`)은 배포 디렉터리 유실로 기동 불가 상태 그대로 둔다(정리는 사용자 판단).
   - 기존 계정·서명 키는 복구할 수 없다.
8. **[발급 서버]** 남은 일: 백업 암호화, 의존성·이미지 취약점 스캔, 감사 로그 보존 정책, 브라우저 E2E 자동화

## 4. 미결 사항

| 항목 | 내용 | 참조 |
|---|---|---|
| 저장소 가시성 | 공개 유지 / 비공개 전환. 비공개로 바꾸면 상세 계획서를 `docs/dev/archive/`로 이전하는 안을 재검토 | D26 |
| 복제 대책 | 다른 사이트·고객사에서 같은 라이선스가 통과함. C2(고객 ID를 SDK 빌드에 내장) 채택 여부 | D24 |
| 갱신 방식 | 세션 풀 증감, `connect` 재호출 안전성, 운영 중 기간 재판정 | D18, `docs/sdk/02-integration.md` §8 |
| `signature` 모드 전제 | SDK의 별도 만료 장치가 **새 형식 라이선스로 연결할 때도** 적용되는지 SDK 저장소에서 확인 | D17 |

## 5. 로컬 전용 검증 (클라우드로 대신할 수 없음)

클라우드 세션은 이 항목들을 완료로 표시하지 않습니다.
- 실제 SDK(`libsonastt_jni_v2.so`)·STT 서버에 적용한 뒤의 수용 기준(`docs/sdk/05-testing.md` §3). 다른 저장소와 이미지가 필요합니다
- 기존 `.key` **실물**과의 대조. 고객 데이터라 저장소에 없고, 저장소 테스트는 합성 데이터를 씁니다
- 사용자 로컬 개발 배포 스택(브라우저 E2E, 봉인·해제 운영 절차, 백업·복원 훈련)
- 운영 키 생성식, 운영 공개키 반영

## 6. 클라우드 시범 운행 지시문 (V5)

> CLAUDE.md와 docs/dev/STATUS.md만 읽고 현재 상태와 다음 작업을 한국어로 요약해 줘. 그다음 CLAUDE.md §3의 검증을 모두 실행하고, 결과(통과 수, 소요 시간, 설치된 도구)를 STATUS.md §2에 "claude.ai/code" 환경 행으로 추가해. `claude/cloud-pilot` 브랜치에 커밋·푸시해 줘.

합격 기준:
- 요약이 이 파일과 일치합니다.
- 검증이 모두 통과합니다.
- 실패하면 원인과 수정안을 보고합니다(임의 수정 금지).

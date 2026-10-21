# 작업 상태 (STATUS)

- **갱신:** 2026-10-21
- **기준:** 이 파일을 갱신한 커밋의 트리(`git log -1 -- docs/dev/STATUS.md`). 이후 커밋이 더 있으면 그 변경을 먼저 확인합니다.
- 결정과 이유는 [DECISIONS.md](DECISIONS.md), 작업 규칙은 [../../CLAUDE.md](../../CLAUDE.md)를 봅니다.

## 1. 구성 요소별 상태

| 구성 요소 | 상태 | 비고 |
|---|---|---|
| 검증 코어 `stt_license` 3.0.0(미발행, 마지막 발행 2.0.2) | 진행 중 | **3.0.0:** `signature` 모드·`VerifySignature`·공개 `ParseStandardFields` 삭제, 문자열 판정은 `VerifyLicense` 하나(D46 1단계 완료, `check_mode`는 다음). 2.x까지: 서명 전용 API, `VerifyMode`, `Utf16ToUtf8`, `Manager`. 공개 헤더 C++11. 공개키 경로 결정 규칙(D29, 1.1.2) — **1.1.1은 상대 `-D` 회귀가 있어 쓰지 않음**. 2.0.0: 라이선스 형식 `license_ver` 1(D34~D36, D38) — `solution`, `total` + **이름 자유 항목**별 세부 항목 상한, 유예 삭제, `1011` 추가, 이전 형식 `1006`. 2.0.1: 발급 유형 `production`·`trial`·`dev` 한정(D40, 검증 동작·API 불변). 2.0.2: 코드의 이전 형식 전용 처리 제거(`license_ver` 없음 → `1005`), 엄격 발급(D41), 1회 발급 기간 상한 `trial` 180일·`dev` 366일(D43 `7e76f0e`, 발급 정책만 — 검증 동작·API 불변). 정식 발행 `sdk-v2.0.2`(2026-10-15) |
| `licensectl` | 완료 | keygen / issue / verify / sign / pubkey. 2.0.0: 새 형식 요약·`-json`·발급 대장, 발급 정책(운영 영구) 강제, `VALID`일 때만 종료 코드 0 |
| JNI 참조 구현·하네스 | 완료 | `connect(callback, configFile, hostLicense)` 형태, (3.0.0) 설정으로 검사 수준을 낮추지 못함 시험, 기존 `.key` 형태 보존 검증. 2.0.0: 엔진별 세션 풀(`sonastt`·`sonaasr`), 엔진 상한·`total`·`1011` 시험 |
| SDK 적용 문서·발행 | 완료 | `docs/sdk/` 01~06(07 업그레이드 안내는 D42로 삭제), `scripts/make-sdk-release.sh`(허용 목록). 정식 발행: 태그 `sdk-v2.0.0`(2026-10-12), `sdk-v2.0.1`(2026-10-13), **`sdk-v2.0.2`(2026-10-15)** |
| 발급 서버 1.7.0 | 완료 | **설치 패키지 발행**(D44): `scripts/make-server-release.sh`, 태그 `server-v1.7.0`, 문서 `docs/release/license-server.md`. 발행 기록은 §2. 로그인·TOTP·봉인 키·발급·감사 해시 체인·임시 비밀번호 만료. Docker 배포. v1.2: 화면 전면 정리(사이드바 앱 셸, 디자인 토큰, 한국어 상태 배지, 폼 묶음·칸별 오류, 위험 작업 구분), 새 발급 폼 오류 시 "갱신 발급" 오표시 수정. v1.3: `license_ver` 1 발급(D37, D38) — 마이그레이션 V3(이전 기록 보존·"이전 형식" 표시), (항목, 세부 항목, 상한) 행 입력, 운영 영구, 문자열 길이 UTF-8 바이트 기준으로 수정. v1.4: 유형 `production`·`trial`·`dev`(D40, DB V4 CHECK), 시험·개발 기간 길이 제한·1년 경고 없음. v1.5: 검증 화면 입력 예시 버그 수정(`format_version` → `license_ver`). v1.6: 이전 형식 지원 제거(D42, DB V5 — 옛 열 삭제·새 열 NOT NULL), `license_ver` 1 단일 형식(`11f47d6`). v1.7: 1회 발급 상한(시험 180일·개발 1년), 개발 총 개발기한(D43, DB V6 `dev_until`), 만료 목록에서 갱신 완료 원본 제외·총 기한 마지막 발급 붉은 알림, 갱신 폼 기간 자동 계산(`7cd4551`) |
| 발급 서버 1.7.0 podman 판 | 완료 | Rocky/RHEL 8(podman 4.9.4, rootful, CNI)용 설치 패키지(D45): `make-server-release.sh --podman`, 구성 `deploy/podman-stack.sh`(pod, NAT 끔), 시험 `scripts/verify-podman-package.sh`, 문서 `docs/release/license-server-podman.md`. 같은 버전·같은 이미지(docker 1.7.0 패키지의 이미지 파일 그대로). 태그 `server-v1.7.0-podman`(푸시됨). `.env`의 `LICENSE_RUNTIME=podman`은 따옴표·공백 없이 그 줄 그대로 써야 합니다(`backup.sh`·`verify-anchors.sh`가 줄 단위로 비교). **실물 서버 확인 필요**: SELinux Enforcing, cgroup v1, 재부팅 자동 시작, firewalld |
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

| 2026-10-06 | 로컬 WSL | 형식 변경(`license_ver` 1): C++ 단위(ASan+UBSan) | 52개 통과. 골든 재생성 후 JDK Ed25519·Python 정규화 교차 검증 일치 |
| 〃 | 〃 | `scripts/check.sh` | `ALL CHECKS PASSED`(7단계, 엔진 상한·`total` `1009`, `1011`, 이전 형식·기간 있는 운영 계약 거부 포함) |
| 〃 | 〃 | `./gradlew test` / `integrationTest` | 62개 / 21개 통과(경계값 일치 시험: 서버 ↔ `licensectl`, 의도적 STRICTER 3건) |
| 〃 | 〃 | 퍼저 `fuzz_parse`(임의 객체 서명·불변식 경로 추가) | 150초 874,083회, 충돌 0 |
| 〃 | 시험 스택(`license-fmt-test`, 이전 이미지로 이전 형식 2건 발급 후 새 이미지로 교체) | 업그레이드·E2E | Flyway V3 0.015초, 기존 행 보존, 제약·트리거 확인, 브라우저 E2E 16/16(이전 기록 표시, 요구 형식·키 순서 그대로 발급, 입력 오류, 시험, 검증 OK/UNSUPPORTED/BAD_SIGNATURE, 갱신, 감사), 콘솔 오류 0·가로 넘침 0 |
| 2026-10-07 | 로컬 WSL | `scripts/make-sdk-release.sh --worktree --verify` (2.0.0) | 풀어낸 SDK 패키지(42개 파일, `stt-license-sdk-2.0.0-dirty`)만으로 `check.sh` 통과: C++ 단위 52, JNI 결합 51항목, LD_PRELOAD 차단·대조군 2종, CMake 소비자 10구성 |
| 2026-10-07 | 로컬 WSL | 항목 커스텀(D38): C++ 단위(ASan+UBSan) | 53개 통과(사용자 정의 항목 Manager 시험 추가). 골든 파일 변경 없음 |
| 〃 | 〃 | `scripts/check.sh` | `ALL CHECKS PASSED`(사용자 정의 항목 발급·`verify -json`, 객체 아닌 항목 거부 추가. 구성 실패 시험 2종이 이전 실행 캐시에 걸리던 문제 수정) |
| 〃 | 〃 | `./gradlew test` / `integrationTest` | 76개 / 21개 통과 |
| 〃 | 시험 스택(`license-fmt-test`, fmt-v1 발급 행이 남은 DB에 새 이미지) | 브라우저 E2E | 23항목 통과(fmt-v1 행 표시 호환, 사용자 정의 항목 발급·검증·갱신, 항목 `total` 거부, 행 보충). 첫 실행 1회는 390px 재로그인 MFA 화면 대기 시간 초과(재실행 시 재현 안 됨, 앱 오류 기록 없음) |
| 〃 | 〃 | 퍼저 `fuzz_parse` | 121초 695,431회, 충돌 0(사용자 정의 항목 시드 포함) |
| 〃 | 〃 | `scripts/make-sdk-release.sh --worktree --verify` (2.0.0, D38 반영) | 풀어낸 SDK 패키지(42개 파일)만으로 `check.sh` 통과: C++ 단위 53, 사용자 정의 항목 E2E, JNI 결합 51항목, CMake 소비자 10구성 |
| 2026-10-12 | 로컬 WSL | 요구 예시 JSON 상호 운용성 검토 | 계약 입력으로 발급·검증 통과(`license_id` 자동). **최종 파일에 `license_id`가 없으면 `full`·Manager `1005`**. 외부 서명(Python 정규화 + OpenSSL 3) → 코어 통과, 코어 발급 → JDK·Node 검증 통과. 이전 SDK(1.1.2): `full` `1005`, `signature` 통과·`offline_stt` 없음. 영구 값: KST 변환 시 Python `OverflowError`(서버 Java는 영향 없음). 실수 `1.0` → `1003`, `\uXXXX` 이스케이프 바이트 서명 → `1004` |
| 2026-10-12 | 로컬 WSL | **SDK 2.0.0 정식 발행** `make-sdk-release.sh --ref sdk-v2.0.0 --verify` | `stt-license-sdk-2.0.0.tar.gz` 43개 파일, 원 커밋 `6cf8475`, SHA-256 `3f9b2b90c81593050f4fd422daf7ca61098ab705f5ea1716972bef4717adc9c5`. 패키지만으로 `check.sh` 통과(C++ 53, JNI 51항목), 재빌드 바이트 동일, 금지 파일 0 |
| 2026-10-13 | 로컬 WSL | 유형 한정·기간 경고 삭제(D40): C++ 단위·`check.sh` | 53개 통과(`dev` 발급·영구 금지, 목록 밖 값·별칭·대소문자 거부, 검증기는 임의 값 통과), `ALL CHECKS PASSED` |
| 〃 | 〃 | `./gradlew test` / `integrationTest` | 82개 / 22개 통과(DB CHECK: 세 값 허용, 그 밖 거부. 5년 시험·개발 경고 없음) |
| 〃 | 시험 스택 재구성(이전 이미지 데이터 → 새 이미지) | 업그레이드·브라우저 E2E | V2 스키마 데이터에서 V3·V4 연속 적용, 행 보존. E2E 26항목 통과(유형 선택 순서, 5년 시험 경고 없음, `dev` 발급·표시), 콘솔 오류 0, 가로 넘침 0. V4 실패 경로(허용 밖 값 존재 → 메시지와 함께 중단·롤백, 정리 후 적용)는 일회용 DB로 확인 |
| 〃 | 〃 | `make-sdk-release.sh --worktree --verify` (2.0.1 사전 점검) | 통과(43개 파일, 패키지 안 이전 유형 문자열 0). 정식 발행(태그 `sdk-v2.0.1`)은 커밋 후 |
| 2026-10-13 | 로컬 WSL | **SDK 2.0.1 정식 발행** `make-sdk-release.sh --ref sdk-v2.0.1 --verify` | `stt-license-sdk-2.0.1.tar.gz` 43개 파일, 원 커밋 `4acc733`, SHA-256 `f6ca8e2157f0878349beb4d5b97e2ca4d6b3f1de8aa60f3e5e1e835d5f360c17`. 패키지만으로 `check.sh` 통과(C++ 53, JNI 51항목), 재빌드 바이트 동일, 금지 파일 0. 2.0.0 대비 공개 헤더·Manager·골든 파일·하네스 동일(바뀐 것은 발급 로직·주석·시험·문서·버전) |
| 2026-10-13 | 로컬 WSL | 코드·주석 `format_version` 제거(D41) | 문서 외 `git grep -iE 'format[_ .-]?version'` 0건. C++ 단위 53, 서버 단위 82·통합 22 통과(엄격 발급: 규격 밖 최상위·`validity` 키 거부·메시지 확인, `license_ver` 없음 `1005`·2 → `1006`, 발급 파일·계약 JSON 키 집합 정확 비교) |
| 〃 | 〃 | `check.sh`·업그레이드 E2E·SDK 사전 점검 | `check.sh` ALL CHECKS PASSED(규격 밖 키 계약 거부 포함). 시험 스택 재구성(V3 주석 변경 → 체크섬, 이전 이미지 데이터 V2 → V3·V4) 후 E2E 28항목 통과(이전 형식 파일 → `LICENSE_INVALID_FIELD`, 검증 화면 예시 `license_ver`), 콘솔 오류 0. SDK 2.0.2 사전 점검 통과(패키지 코드에 `format_version` 0). 퍼저 61초 354,322회 충돌 0. 정식 발행(태그)은 요청 시 |
| 2026-10-13 | 로컬 WSL | 이전 버전 호환 제거(D42) | C++ 단위 53·`check.sh` 통과, 서버 단위 82·통합 22(빈 DB에 V1~V5, 스키마: 옛 열 없음·새 열 NOT NULL). V5 실패 경로(`license_ver` 없는 행 → 중단·롤백) / 성공 경로(옛 열·제약 삭제, 트리거 동작)는 일회용 DB로 확인. 시험 스택 V4 이미지로 4건 발급 → V5 이미지로 교체: 행 보존, E2E 24항목 통과. SDK 2.0.2 사전 점검 통과(42개 파일, 07 삭제·링크 정상) |
| 2026-10-14 | 로컬 WSL | 1회 발급 상한·총 개발기한(D43) | C++ 단위 53(경계: 시험 180일·개발 366일 허용, +1초 거부, 윤일 포함 1년 허용, 검증기는 길이 무시)·`check.sh` ALL CHECKS PASSED. 서버 단위 93(경계값 일치 시험에 상한 5건·STRICTER 3건 추가)·통합 24(빈 DB V1~V6: `dev_until` CHECK·불변·앱 권한 거부, 갱신 완료 원본 만료 목록 제외). 시험 스택 V5 → V6 이미지 교체(0.023초, 기존 개발 행 NULL 유지) 후 E2E 34항목 통과(상한·총 기한 오류, 파일에 총 기한 없음, 대시보드 붉은 알림, 갱신 폼 180일·총 기한 복사, 갱신 원본 제외), 콘솔 오류 0·가로 넘침 0 |
| 2026-10-15 | 로컬 WSL | D42·D43 검토·주제별 커밋 | 문서 잔재 정리(SDK README 정책 표에 `full` 안내, 04 목록 공백, 낡은 테스트 주석, STATUS "배포 순서" 미결 삭제 — D42로 대상 없음). 커밋을 주제별로 나누고 각 상태를 별도 worktree에서 확인: D42 단독(`11f47d6`·`dedc8b3`·`ba51f05`) C++ 단위 53·서버 단위 82·통합 22, D43 포함 최종(`7e76f0e`·`7cd4551`) C++ 단위 53·서버 단위 93·통합 24 통과 |
| 2026-10-15 | 로컬 WSL | **SDK 2.0.2 정식 발행** `make-sdk-release.sh --ref sdk-v2.0.2 --verify` | `stt-license-sdk-2.0.2.tar.gz` 42개 파일, 원 커밋 `297212e`, SHA-256 `773e645f71937ebf3f87c58dc943b0a28b4eb166a0152f34937659ca3ba0dfee`. 패키지만으로 `check.sh` ALL CHECKS PASSED, 재빌드 바이트 동일, 금지 파일·비밀키 0(`golden_license.lic`는 시험 벡터). 2.0.1 대비 골든 파일·하네스 동일, 공개 헤더는 주석만, `1006` 오류 메시지 문구 변경(D41), 발급 상한(D43), 07 삭제 |
| 〃 | 〃 | 사용자 개발 스택 `license-dev` 이미지 교체(사용자 요청) | 교체 전 백업(덤프 24 KB, 감사 앵커 기록). `297212e`에서 이미지 빌드 → `up -d --wait web`: Flyway V2 → V3·V4·V5·V6(0.046초), healthy, 로그인 200, WARN/ERROR 0. 계정 1·감사 1·라이선스 0 보존, `dev_until`·CHECK 확인. 발견: `backup.sh`는 `-p` 없이 `docker compose`를 불러 compose 기본 이름 스택을 겨냥 → 서버 README에 `COMPOSE_PROJECT_NAME` 안내 |
| 2026-10-19 | 로컬 WSL | **발급 서버 1.7.0 정식 발행** `make-server-release.sh --ref server-v1.7.0 --verify` | `license-server-1.7.0.tar.gz` 232 MB, 22개 파일(이미지 2 + 배포 구성 5 + 문서 12 + 생성 3), 원 커밋 `6b9fc33`, SHA-256 `783f6be627e8d0fb92fb0fd304951a70a0f461069c6736fe5fb179e5ff8b67b4`. 이미지 `license-server:1.7.0`(`sha256:3cd90ccd…`, 라벨 revision = 원 커밋), `postgres:16-alpine`(`postgres@sha256:721873c3…`). 패키지만으로 설치·기동: 비밀값 생성·키 생성식(`seal-key --passphrase-file`)·healthy·로그인 200·마이그레이션 6·경고/오류 0, 시험 스택 삭제 확인. 금지 파일·비밀키(문서·jar·`licensectl`)·링크 검사 통과. 같은 소스에서 서버 단위 93·통합 24 통과. 발견·수정: `docker save -o`의 0600 권한(`6b9fc33`, 발행 전 태그 이동) |
| 2026-10-21 | 로컬 WSL + Rocky 8.10 컨테이너 | **발급 서버 1.7.0 podman 판 발행** `make-server-release.sh --ref server-v1.7.0-podman --podman build/server-release/license-server-1.7.0.tar.gz --verify` | `license-server-1.7.0-podman.tar.gz` 232 MB, 25개 파일, 원 커밋 `69eb452`, SHA-256 `969061628ef42f377066d39e499f1bc5e046f6a88594ee89b55e7f6dfe32167e`(재생성 바이트 동일). 이미지 파일은 docker 1.7.0 패키지와 바이트 동일(서버 이미지 `sha256:3cd90ccd…`, 원 커밋 `6b9fc33`). 시험(Rocky 8.10, podman 4.9.4-rhel, CNI, `--privileged` 컨테이너): healthy, 로그인 200, 마이그레이션 6, 경고·오류 0, 외부 송신 차단, pod 밖 DB 차단, 백업·앵커 대조 통과. docker 판 사전 점검(`--worktree --verify`)도 백업·앵커 대조 포함 통과. 시험 중 고친 것: internal 네트워크 게시 불가 → NAT 끈 일반 네트워크, 읽기 전용 루트의 비밀값 mount 자리 → `/run/secrets` tmpfs, 사전 점검 판의 이미지 태그 불일치 |
| 2026-10-21 | 로컬 WSL | **`signature` 모드 삭제(코어 3.0.0 1단계)** `scripts/check.sh` / 퍼저 / `./gradlew test` | `ALL CHECKS PASSED`(7단계): C++ 단위(ASan+UBSan) 52개(모드 시험 2개 삭제, 출력 시험 1개 추가), JNI 결합 49항목(옛 설정 `--license-verify-mode=signature` + 만료 → `1008` 포함), JNI export `Java_*` 10개만, LD_PRELOAD 차단·대조군 2종, CMake 소비자 4구성·키 누락·비밀키 오지정. 퍼저(clang, 60초 1,711만 회) 충돌 0. 발급 서버 단위 93개 통과(새 `licensectl`). `make-sdk-release.sh --worktree --verify`(3.0.0-dirty): 풀어낸 패키지만으로 `check.sh` 통과(JNI 49항목). 골든 파일 변경 없음 |

## 3. 다음 작업 (우선순위)

0. **[완료 2026-10-15]** 로컬 개발 스택 `license-dev`를 `297212e` 이미지(서버 v1.7)로 교체 — V3~V6 적용, 계정·감사 기록 보존. 교체 전 백업·이전 이미지 태그(`license-server:dev-pre-v6`) 보관
1. **[이 저장소, SDK 첫 적용 전]** 코어 3.0.0(D46, `forward-compat.md` §9). **1단계 완료 2026-10-21**: `signature` 모드 삭제(`remove-signature-mode.md`). 남은 것: `check_mode`·`minimal` 판정, `licensectl verify` 출력, `sdk-v3.0.0` 발행. 전달 전에 SDK 팀이 2.0.2로 적용을 시작하지 않았는지 확인
2. **[사용자]** 저장소 가시성 결정(현재 **공개**). 공개 이력에 남은 내용 처리 여부 포함 — D26
3. **[사용자]** claude.ai/code 환경 생성(GitHub 접근, 네트워크 Trusted, setup script `bash scripts/cloud-setup.sh`, 비밀 없음) → §6 시범 운행
4. **[결정 대기]** 복제 대책 C2 채택 여부(D24), 라이선스 갱신 방식(D18)
5. **[SDK 저장소]** 적용 S1~S6(`connect` 순서 규칙 `docs/sdk/02-integration.md` §4, 2026-10-21 흐름 검토: 판정 먼저·성공 후 상태 반영·채널 값 적용·재호출 거부·콜백 전역 참조·오류 코드 범위 확인): CMake 결합(공개키는 `docs/sdk/01-build.md` §6.3 방식), 심볼 은닉, `connect(hostLicense)`, 기존 `.key` 공존, 예약 타임아웃, 서버 오류 처리
6. **[이 저장소]** CI 구성(`check.sh`, Gradle 두 작업. 공개 저장소면 GitHub Actions 무료)
7. **[완료 2026-10-01]** 로컬 PC의 Google Drive 미러 동기화가 만든 옛 사본 6개를 삭제했다(git 이력 일치 확인 후).
   - 재발 방지: 로컬 작업 폴더의 git 데이터를 동기화 폴더 밖으로 분리했다(D31).
   - 클라우드 세션과는 무관하다.
8. **[완료 2026-10-02]** 로컬 개발 스택을 영구 위치에 새로 구성했다(프로젝트 `license-dev`, 포트 8443, 새 UI 이미지).
   - 이전 스택(`license-server`)은 배포 디렉터리 유실로 기동 불가 상태 그대로 둔다(정리는 사용자 판단).
   - 기존 계정·서명 키는 복구할 수 없다.
9. **[발급 서버]** 남은 일: 백업 암호화, 의존성·이미지 취약점 스캔, 감사 로그 보존 정책, 브라우저 E2E 자동화

## 4. 미결 사항

| 항목 | 내용 | 참조 |
|---|---|---|
| 저장소 가시성 | 공개 유지 / 비공개 전환. 비공개로 바꾸면 상세 계획서를 `docs/dev/archive/`로 이전하는 안을 재검토 | D26 |
| 복제 대책 | 다른 사이트·고객사에서 같은 라이선스가 통과함. C2(고객 ID를 SDK 빌드에 내장) 채택 여부 | D24 |
| 갱신 방식 | 세션 풀 증감, `connect` 재호출 안전성, 운영 중 기간 재판정 | D18, `docs/sdk/02-integration.md` §8 |
| 단기 변경 발급(부하 시험 등) | 제안·사용자 검토 대기: 기존 개발 라이선스는 그대로 두고 **새 발급**(갱신 발급 아님)으로 같은 과제의 `dev`, 같은 총 개발기한, 시험 채널, 시험 기간만. 시험 뒤 원래 파일로 되돌려 재시작하고 장부에서 무효 처리. 근거: 갱신 발급은 `renewed_from` 때문에 원본이 만료 알림에서 빠짐(`LicenseRepository.expiringBetween`), 오프라인이라 폐기 불가, `full`도 `connect` 때만 기간 판정 | D18, D22, D24, D43 |
| 형식 변경 후 전방 호환(이전 SDK가 재빌드 없이 이후 발급분을 받음) | **결정됨(2026-10-21, D46), 구현 전.** 안 나: 추가형 진화(A1~A10) + 서명된 `check_mode`(`full`/`minimal`) + 검사 수준은 라이선스가 정함(설정 `--license-verify-mode`·`Verify(VerifyMode)` 삭제). 안정 필드: `check_mode`, `license_id`, `allowed_channels`, `validity.not_before`·`not_after`(`license_type` 제외). 코어 2.1.0을 SDK 첫 적용 전에 전달. 코드 대조 검토로 계획 오류 R1~R10을 고침 | [forward-compat.md](forward-compat.md) §9(구현 계획), §10(바로잡은 것), D46 |
| `signature` 모드 전제 | **해소(2026-10-21, D46, 코어 3.0.0 미발행):** `signature` 모드와 `VerifySignature`를 삭제해 새 형식의 기간은 항상 라이브러리가 판정. SDK 별도 만료 장치는 기존 `.key` 경로에만 남음(영구 값 9999년 처리 확인은 그 경로에 한해 유효) | D17(대체됨), D46, [remove-signature-mode.md](remove-signature-mode.md) |

## 5. 로컬 전용 검증 (클라우드로 대신할 수 없음)

클라우드 세션은 이 항목들을 완료로 표시하지 않습니다.
- 실제 SDK(`libsonastt_jni_v2.so`)·STT 서버에 적용한 뒤의 수용 기준(`docs/sdk/05-testing.md` §3). 다른 저장소와 이미지가 필요합니다
- 기존 `.key` **실물**과의 대조. 고객 데이터라 저장소에 없고, 저장소 테스트는 합성 데이터를 씁니다
- 사용자 로컬 개발 배포 스택(브라우저 E2E, 봉인·해제 운영 절차, 백업·복원 훈련)
- 운영 키 생성식, 운영 공개키 반영
- 발급 서버 podman 판의 실물 Rocky 8.10 확인: SELinux Enforcing에서 bind mount 라벨, cgroup v1 메모리 제한, 재부팅 후 `podman-restart.service` 자동 시작과 health 타이머, firewalld. 설치 서버에서 `sudo scripts/verify-podman-package.sh <패키지>`(패키지에 포함)로 앞의 셋을 먼저 확인합니다(`docs/release/license-server-podman.md` §6)

## 6. 클라우드 시범 운행 지시문 (V5)

> CLAUDE.md와 docs/dev/STATUS.md만 읽고 현재 상태와 다음 작업을 한국어로 요약해 줘. 그다음 CLAUDE.md §3의 검증을 모두 실행하고, 결과(통과 수, 소요 시간, 설치된 도구)를 STATUS.md §2에 "claude.ai/code" 환경 행으로 추가해. `claude/cloud-pilot` 브랜치에 커밋·푸시해 줘.

합격 기준:
- 요약이 이 파일과 일치합니다.
- 검증이 모두 통과합니다.
- 실패하면 원인과 수정안을 보고합니다(임의 수정 금지).

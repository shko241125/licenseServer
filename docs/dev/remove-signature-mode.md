# `signature` 모드 삭제: 구현 계획과 결과 (코어 3.0.0)

- **상태:** 구현 완료·검증 통과(2026-10-21), 커밋·발행 전. 결과는 §6.
- **요청(사용자, 2026-10-21):** 과거의 전혀 다른 라이선스(기존 `.key`)를 위해 서명만 확인하던 `signature` 모드를 구현 예정 문서와 코드에서 삭제한다. 초기 구현 기록(변경 이력, 지난 결정, 검증 기록, 로컬 계획서의 지난 절)은 그대로 둔다.
- 관련 결정: D46(전방 호환 — 검사 수준은 라이선스가 정함). 이 작업은 D46 구현 범위 중 **검증 수준 선택 제거**를 먼저 한 것입니다. `check_mode` 판정은 다음 작업입니다(`forward-compat.md` §9).

## 1. 무엇이 `signature` 모드인가

코드와 문서에서 "서명만 보고 라이선스를 받아 주는 경로"는 세 가지 형태로 있습니다.

| 형태 | 위치 | 하는 일 |
|---|---|---|
| 모드 선택 API | `VerifyMode`, `Verify(text, mode, …)`, `ParseVerifyMode`, `VerifyModeName`(공개 헤더), `VerifyWith`(내부) | `SignatureOnly`면 서명만 |
| 서명 전용 공개 함수 | `VerifySignature`(공개 헤더, `embedded_key.cpp.in`) | 서명만 보고 `VerifiedLicense`를 줌 |
| 서명 전용 결과에 쓰는 도우미 | `ParseStandardFields(VerifiedLicense)`(공개 헤더) | 서명만 본 결과에 표준 규칙을 "선택적으로" 적용 |
| SDK 설정 | `--license-verify-mode=signature|full`(없으면 `signature`), 하네스 `ReadVerifyMode` | 고객 설정 파일로 검사 수준을 고름 |

- `VerifySignature`도 삭제합니다. 이름만 다를 뿐 "서명만 맞으면 연결 허용"을 그대로 할 수 있는 함수이기 때문입니다. 남겨 두면 모드를 지운 효과가 없습니다.
- `ParseStandardFields`는 공개 헤더에서 빼고 내부(`detail.h`)로 옮깁니다. 서명 전용 결과와 짝을 이루는 도우미라, `VerifySignature`가 없으면 쓸 곳이 없습니다(`VerifyLicense`가 이미 같은 판정을 합니다).
- 서명 검증 자체(`VerifyTree`, 내부 `VerifySignatureWith`)는 그대로입니다. 모든 판정의 첫 단계이고, 시험·퍼저·도구가 씁니다. `detail.h`는 SDK에 배포하지 않습니다.

## 2. 남는 공개 API

| API | 하는 일 |
|---|---|
| `VerifyLicense(text, &license, &fields, &detail)` | **문자열 방식의 유일한 판정.** 서명 + 표준 필드 + 기간. 성공할 때만 출력을 채움 |
| `VerifiedLicense::GetString/GetInt/GetBool/Has`, `canonical_json()` | `VerifyLicense`를 통과한 라이선스에서 값 조회(추가형 새 필드를 새 SDK가 읽을 때도 씀) |
| `LicenseFields`, `StateAt(fields, now)` | 판정된 값, 운영 중 기간 재판정 |
| `Utf16ToUtf8` | JNI 문자열 변환(D16) |
| `Manager`, `Channel`, `Info`, `InfoToJson` | 파일 경로 방식(변경 없음) |
| `Error`, `ErrorName`, `ErrorMessage`, `StateName` | 변경 없음. `Error` 순번 불변 |

기존 `.key` 공존(D16)은 그대로입니다. `VerifyLicense`가 `ParseError`를 낼 때만 기존 경로로 보냅니다. 판정 순서상 문법 검사가 서명·필드보다 먼저라서 분기 조건이 바뀌지 않습니다.

## 3. 버전: 3.0.0

- 공개 헤더 API를 삭제하므로 x를 올립니다(`docs/release/sdk.md` 버전 규칙: 공개 헤더 API 호환이 깨지면 x).
- D46과 `forward-compat.md`가 "코어 2.1.0"이라고 적은 것은 이 규칙과 어긋난 오류였습니다. 3.0.0으로 고칩니다. `check_mode`도 같은 3.0.0에 넣습니다(미발행 상태에서 이어서 구현).
- SDK 적용 전이라 깨지는 SDK는 없습니다(D46 결정 4).

## 4. 변경 목록

### 4.1 코드
| 파일 | 변경 |
|---|---|
| `CMakeLists.txt` | `VERSION 3.0.0` |
| `core/include/stt_license/license.h` | 위 §1 API 삭제. 머리말의 두 방식 설명을 "(A) 문자열 판정 `VerifyLicense` / (B) 파일 경로 `Manager`"로. `VerifiedLicense` 주석: `VerifyLicense`가 통과시킨 결과만 담김 |
| `core/src/detail.h` | `ParseStandardFields` 선언을 내부로 옮김, `VerifyWith` 삭제 |
| `core/src/license.cpp` | `VerifyWith`·`VerifyModeName`·`ParseVerifyMode` 삭제. 주석 정리 |
| `core/src/embedded_key.cpp.in` | `VerifySignature`·`Verify` 삭제. `VerifyLicense`가 내장 키로 `VerifyLicenseWith`를 직접 부름. 링크 확인 주석 갱신 |
| `core/fuzz/fuzz_parse.cpp` | `VerifyWith(Full)` → `VerifyLicenseWith` |
| `core/tests/verify_test.cpp` | `VerifyMode` 시험 삭제. 서명 단계 시험은 내부 `VerifySignatureWith`로 유지. "서명만 통과하면 비표준 값도 Ok" 시험을 "`VerifyLicense`는 비표준 값·만료를 거부"로 바꿈 |
| `integration/harness/harness_jni.cpp` | `ReadVerifyMode`·설정 읽기 삭제. `connectHostLicense`는 `VerifyLicense`로 판정하고 `LicenseFields`를 보관. 상태 JSON에서 `mode`·`standard` 삭제. 대조군 `verifyViaModifiedUtf8`도 `VerifyLicense` |
| `integration/harness/java/.../HarnessTest.java`, `SttHarness.java` | 모드 선택 시험을 "설정 파일에 옛 키가 있어도 무시, 만료 `1008`"로 바꿈 |
| `scripts/check.sh` | `full.cfg`·`bad.cfg` 생성 삭제, `sig.cfg`는 "옛 설정 무시" 시험용으로 유지 |

### 4.2 문서
| 문서 | 변경 |
|---|---|
| `docs/sdk/02-integration.md` | §1 흐름, §2 API 표, §4 `connect` 예, §5 "검증 수준 선택" 삭제 → "검사 수준은 라이브러리가 정함", §7 값 사용, §9 오류 표 |
| `docs/sdk/03-stt-server.md` | §2 `1007`·`1008`, §3 설정 절 삭제, §5, §6 확인 목록 |
| `docs/sdk/04-license-format.md` | `signature` 모드 언급(§5, `license_id` 행) |
| `docs/sdk/05-testing.md` | 동작 표의 `signature` 열 삭제 |
| `docs/sdk/06-field-operations.md` | `1007`·`1008` 설명 |
| `docs/sdk/01-build.md` | `embedded` 타깃에 정의된 함수 목록, 링크 오류 예 |
| `docs/sdk/README.md` | 예제, 문서 표, 핵심 결정 표, 변경 이력에 3.0.0(미발행) |
| `docs/dev/forward-compat.md` | 버전 3.0.0, §7.3·§9의 삭제 범위(`VerifySignature`·`ParseStandardFields` 포함), 이 작업 완료 표시 |
| `docs/dev/DECISIONS.md` | D46의 버전·삭제 범위 정정(아직 커밋하지 않은 초안), D15 대체 사유 보완. D16은 그대로(공존 분기 조건 불변) |
| `docs/dev/STATUS.md` | 코어 행, 검증 기록, 다음 작업 |

### 4.3 바꾸지 않는 것
- 지난 기록: `README.md` §6의 1.1.0~2.0.2 행, DECISIONS의 D15·D17(이미 "대체됨"), STATUS §2의 지난 검증 행, 로컬 계획서 §20 이전 절.
- `licensectl`: 서명 전용 경로가 없습니다(`verify`는 `ParseAndVerify` = 전체 판정).
- 발급 서버: `licensectl` 출력이 같으므로 영향 없음.
- 골든 파일, `Error` 순번.

## 5. 위험과 확인 방법

| 위험 | 확인 |
|---|---|
| 남은 호출부가 있어 빌드가 깨짐 | 전체 빌드(코어, 시험, 퍼저, 하네스, CMake 소비자) = `scripts/check.sh` |
| 서명 단계 시험이 사라져 회귀를 못 잡음 | 서명 단계 시험(변조, 다른 키, 64 KiB, 문법, BOM, 골든 정규화)은 내부 `VerifySignatureWith`와 `VerifyLicense`로 유지 |
| 공존 분기 조건(`ParseError`)이 바뀜 | 하네스: 기존 `.key` 형태 → `1003`, 빈 문자열 → `1003` |
| 설정으로 검사를 낮추는 길이 남음 | 하네스: `--license-verify-mode=signature`가 든 설정 + 만료 파일 → `1008` |
| 링크 확인(키 누락 시 링크 실패)이 약해짐 | `VerifyLicense`·`Manager::Open`이 `embedded_key.cpp.in`에만 정의됨. `check.sh`의 "키 누락" 구성 실패 시험 |
| 문서에 옛 API가 남음 | `grep`으로 `VerifyMode`·`VerifySignature`·`ParseStandardFields`·`license-verify-mode`·`signature` 모드 검색, 지난 기록만 남았는지 확인 |

## 6. 결과 (2026-10-21, 로컬 WSL)

| 검증 | 결과 |
|---|---|
| `scripts/check.sh` | `ALL CHECKS PASSED`(7단계, 약 1분) |
| C++ 단위(ASan+UBSan) | 52개 통과. 모드 시험 2개(`VerifyMode.*`) 삭제, `VerifyLicense.OutputsOnlyOnSuccess` 추가, 서명 단계 시험은 `SignatureStage.*`로 유지하고 "서명 단계는 통과하지만 `VerifyLicense`는 거부" 확인 추가 |
| 골든 벡터 교차검증 | 통과(골든 파일 변경 없음) |
| JNI 하네스 | export `Java_*` 10개만. 결합 시험 49항목 통과: 옛 설정 `--license-verify-mode=signature` + 만료 → `1008`, 유효 → 0, 변조 → `1004`, 기존 `.key` 형태·빈 문자열 → `1003`(공존 분기 조건 유지), 판정된 `LicenseFields` 사용 |
| LD_PRELOAD | 보호 빌드 `1004`, 대조군 2종 우회(차단 효과 확인) |
| CMake 소비자 | C++11~20 공개 헤더 `-Wpedantic -Werror`, 4구성, 키 누락·비밀키 오지정 구성 실패 |
| 퍼저 | clang `-fsanitize=fuzzer` 빌드, 60초 1,711만 회 충돌 0 |
| 발급 서버 | 새 `licensectl`로 `./gradlew test` 93개 통과(`licensectl` 동작 변경 없음) |
| SDK 패키지 | `make-sdk-release.sh --worktree --verify`: 풀어낸 패키지(3.0.0-dirty)만으로 `check.sh` 통과 |
| 남은 참조 | `grep` 결과 옛 API 이름은 내부(`detail.h`의 `VerifySignatureWith`·`ParseStandardFields`), 시험, 지난 변경 이력과 "삭제했다"는 안내에만 남음 |

구현 중 바로잡은 것:
- 처음 쓴 시험이 한 `VerifiedLicense` 변수를 서명 단계 성공 뒤 `VerifyLicense` 실패에 다시 써서 `valid()`가 참으로 남았다. "실패하면 출력을 건드리지 않음"은 의도된 계약이므로 시험을 고쳤다(변수 분리).
- 하네스 Java 시험을 고치는 중 같은 문자열(`sdk.disconnect();`)이 앞쪽에도 있어 내용이 겹쳐 들어갔다. 원본으로 되돌린 뒤 검색 시작 위치를 지정해 다시 고쳤다(최종 차분 11줄 추가·14줄 삭제).

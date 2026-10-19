# SDK 배포 (stt-license-sdk)

STT SDK 팀에 검증 코어(`stt_license`)를 넘기는 방식과 방법, 배포 파일을 정리합니다. 발급 서버 배포는 [license-server.md](license-server.md)를 봅니다.
- 포함·제외 규칙과 그 이유, 스크립트의 자동 검사: [../sdk/RELEASE_MANIFEST.md](../sdk/RELEASE_MANIFEST.md)(패키지에도 들어감)
- 이 문서는 발행 담당용이며 SDK 패키지에는 들어가지 않습니다.

## 1. 개요

| 항목 | 내용 |
|---|---|
| 산출물 | `stt-license-sdk-<버전>.tar.gz` + `stt-license-sdk-<버전>.tar.gz.sha256` |
| 받는 사람 | STT SDK(C++/JNI) 개발자, STT 서버(Java) 개발자, 현장 설치 엔지니어 |
| 내용 | 소스(코어·`licensectl`), 테스트·골든 벡터, 참조 구현, 검증 스크립트, 적용 문서 |
| 따로 보내는 것 | 운영 공개키 `public.key`(Base64 44자)와 그 SHA-256 지문. 서로 다른 경로로 보냄 |
| 보내지 않는 것 | 비밀키, 발급 서버, 고객 라이선스, 내부 계획 자료 |
| 버전 원본 | `CMakeLists.txt`의 `project(stt_license VERSION x.y.z)` |
| 태그 | `sdk-v<버전>` (주석 태그) |
| 생성 도구 | `scripts/make-sdk-release.sh`, 담을 목록 `scripts/sdk-release-files.txt` |

## 2. 배포 방식

| 방식 | 내용 | 이유 |
|---|---|---|
| 소스 배포 | 빌드 결과물이 아니라 소스를 보냅니다. SDK가 자기 CMake에 서브프로젝트로 넣고 빌드합니다 | 공개키는 SDK 빌드 때 내장됩니다(D27). glibc·OpenSSL을 SDK 빌드 환경에 맞춥니다 |
| 허용 목록 | 목록에 적힌 경로만 담습니다. 새 파일은 기본으로 빠집니다 | 발급 서버·내부 문서가 섞여 나가지 않게(D25) |
| 커밋 기준 | 태그 커밋의 내용(`git archive`)만 담습니다. 작업 트리로 만든 것(`--worktree`)은 버전에 `-dirty`가 붙고 발행하지 않습니다 | 패키지가 어느 커밋인지 `VERSION`으로 증명 |
| 재현 가능 | 파일 순서·소유자·시각을 고정하고 `gzip -n`으로 압축합니다. 같은 태그로 다시 만들면 바이트까지 같습니다 | 받는 쪽·감사 때 원 커밋에서 다시 만들어 대조 |
| 단독 검증 | 풀어낸 패키지만으로 `scripts/check.sh` 7단계를 통과해야 발행합니다 | 수용 기준(05-testing.md)을 패키지만으로 재현 |
| 공개키 분리 | 공개키는 패키지와 다른 경로로, 지문은 또 다른 경로로 보냅니다 | 한 경로가 바꿔치기돼도 지문 대조로 드러남(01-build.md §6.2) |

## 3. 배포 방법

### 발행하는 쪽 (발행 담당)
1. 버전을 정합니다. `CMakeLists.txt`의 버전과 `docs/sdk/README.md` §6 변경 이력을 함께 고칩니다.
   - 공개 헤더 API·오류 코드 순번·라이선스 규격의 호환이 깨지면 x, API 추가는 y, 수정만 있으면 z를 올립니다.
2. 커밋하고 태그를 답니다: `git tag -a sdk-v<버전> -m "stt_license SDK <버전>"`
3. 패키지를 만들고 검증합니다(리눅스, 도구는 05-testing.md §2).
   ```bash
   scripts/make-sdk-release.sh --ref sdk-v<버전> --verify
   # → build/sdk-release/stt-license-sdk-<버전>.tar.gz (+ .sha256), 마지막에 "verified: ..."
   ```
4. 수용 기준([RELEASE_MANIFEST.md](../sdk/RELEASE_MANIFEST.md) §5)을 확인하고 태그를 원격에 올립니다.
5. `.tar.gz`와 `.sha256`을 SDK 팀에 전달합니다. 공개키가 바뀌었을 때만 `public.key`와 지문을 따로 보냅니다.
6. `docs/dev/STATUS.md`에 발행 기록(커밋·해시·검증)을 남깁니다.

### 받는 쪽 (SDK 팀)
```bash
sha256sum -c stt-license-sdk-<버전>.tar.gz.sha256        # 전달 중 손상·바꿔치기 확인
tar -xzf stt-license-sdk-<버전>.tar.gz && cd stt-license-sdk-<버전>
sha256sum --quiet -c SHA256SUMS                           # 패키지 안 파일 대조
cat VERSION                                               # 버전과 원 커밋
scripts/check.sh                                          # 선택: 전체 검증, 마지막 줄 "ALL CHECKS PASSED"
```
이후 적용은 `docs/sdk/README.md`(빠른 시작)와 01-build.md를 따릅니다.

## 4. 배포 파일 목록

### 전달 파일
| 파일 | 내용 |
|---|---|
| `stt-license-sdk-<버전>.tar.gz` | 패키지. 최상위 디렉터리 `stt-license-sdk-<버전>/` |
| `stt-license-sdk-<버전>.tar.gz.sha256` | 위 파일의 SHA-256 (`sha256sum -c` 형식) |
| `public.key` *(별도, 바뀔 때만)* | 운영 공개키. 지문은 다른 경로로 |

### 패키지 안 (2.0.2 기준 42개)
| 경로 | 파일 | SDK 빌드에 필요 |
|---|---|---|
| `README.md`, `VERSION`, `SHA256SUMS` *(생성)* | 루트 안내, 버전·원 커밋, 파일 해시 | |
| `CMakeLists.txt`, `.gitattributes` | 빌드 진입점·버전·옵션, `*.sh` LF 유지 | ● (`CMakeLists.txt`) |
| `cmake/SttLicenseHardening.cmake` | 심볼 은닉 함수 | ● |
| `core/include/stt_license/license.h` | 공개 헤더(C++11 이상) | ● |
| `core/src/` | `crypto.cpp/.h`, `detail.h`, `embedded_key.cpp.in`, `json.cpp/.h`, `license.cpp`, `manager.cpp` | ● |
| `core/tests/` | `json_test.cpp`, `license_test.cpp`, `manager_test.cpp`, `verify_test.cpp`, `test_util.h`, `crosscheck/GoldenCheck.java` | |
| `core/testdata/` | `golden_canonical.json`, `golden_license.lic`, `golden_public.key`(정규화 호환 증거), `contract.example.json` | |
| `core/fuzz/fuzz_parse.cpp` | libFuzzer 대상 | |
| `tools/licensectl/main.cpp` | 개발·검증용 CLI | |
| `integration/harness/` | `harness_jni.cpp`, `java/licenseharness/{HarnessTest,SttHarness}.java` | |
| `integration/cmake-consumer/` | `CMakeLists.txt`, `mock_sdk.cpp`, `smoke.cpp` | |
| `scripts/check.sh` | 전체 검증 7단계 | |
| `docs/sdk/` | `README.md`, `01-build.md`~`06-field-operations.md`, `RELEASE_MANIFEST.md` | |

정확한 목록은 패키지의 `SHA256SUMS`가 원본입니다(`tar -tzf <패키지>`로도 확인).

## 5. 발행 이력

| 버전 | 태그 → 커밋 | 파일 수 | 패키지 SHA-256 | 비고 |
|---|---|---|---|---|
| 2.0.2 | `sdk-v2.0.2` → `297212e` | 42 | `773e645f71937ebf3f87c58dc943b0a28b4eb166a0152f34937659ca3ba0dfee` | 2026-10-15. 공개키 변경 없음 |

이전 버전의 변경 내용은 [../sdk/README.md](../sdk/README.md) §6을 봅니다.

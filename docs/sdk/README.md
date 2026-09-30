# STT 라이선스 SDK 모듈 (stt_license) — 적용 안내

폐쇄망 STT 제품용 **오프라인 라이선스 검증 모듈**입니다.
- 개발사가 Ed25519로 서명한 평문 JSON 라이선스(`license.lic`)를 STT SDK가 **내장 공개키로 스스로 검증**합니다.
- 서버 연결 없이 동작합니다.

| 항목 | 내용 |
|---|---|
| 버전 | 1.1.1 (`CMakeLists.txt`의 `project(... VERSION ...)`) |
| 대상 | STT SDK(C++/JNI, `libsonastt_jni_v2.so`) 개발자, STT 서버(Java) 개발자, 현장 설치 엔지니어 |
| 받는 것 | 이 패키지(소스·문서·검증 도구) + 발급 담당이 주는 **공개키 파일 `public.key`**(Base64 44자) |
| 받지 않는 것 | 비밀키, 발급 서버. 라이선스 발급은 발급 담당(사내 발급 서버)이 합니다 |

## 1. 빠른 시작 (SDK 개발자)

```cmake
# 1) SDK CMakeLists.txt — JNI 라이브러리 타깃 정의 뒤
set(STT_LICENSE_PUBLIC_KEY_FILE ${CMAKE_SOURCE_DIR}/keys/stt_license_public.key)   # 운영 공개키 고정 (01-build.md §6)
add_subdirectory(third_party/stt-license-sdk EXCLUDE_FROM_ALL)
target_link_libraries(sonastt_jni_v2 PRIVATE stt_license::embedded)
stt_license_harden_shared_library(sonastt_jni_v2)     # 필수: JNI 진입점 외 심볼 숨김
```
```cpp
// 2) JNI connect(callback, configFile, hostLicense) 안에서
std::string text;                                               // jstring → 표준 UTF-8 (GetStringUTFChars 금지)
lic::Error e = JStringToUtf8(env, hostLicense, &text);
lic::VerifiedLicense license;  lic::LicenseFields fields;
if (e == lic::Error::Ok) e = lic::Verify(text, mode /* SignatureOnly | Full */, &license, &fields);
if (e != lic::Error::Ok) return 1000 + static_cast<int>(e);    // 연결 거부
```
```bash
# 3) 확인
nm -D --defined-only libsonastt_jni_v2.so | awk '{print $3}' | grep -v '^Java_'   # 출력이 없어야 함
scripts/check.sh                                                                   # 이 패키지 전체 검증
```
자세한 내용은 아래 문서를 봅니다.

## 2. 문서 지도

| 문서 | 내용 | 주 독자 |
|---|---|---|
| [01-build.md](01-build.md) | SDK CMake에 넣기, 옵션, OpenSSL 재사용, 심볼 은닉, 빌드 환경, 문제 해결 | SDK |
| [02-integration.md](02-integration.md) | 코드 결합: 검증 API, `connect(hostLicense)` 구현, 검증 수준(`signature`/`full`), 기존 `.key` 공존, 오류 코드 | SDK |
| [03-stt-server.md](03-stt-server.md) | STT 서버(Java) 측: 라이선스 읽기·전달, 오류 처리, 설정, Docker | 서버 |
| [04-license-format.md](04-license-format.md) | 라이선스 파일 규격(필드·타입·범위), 정규화·서명 방식, 호환 규칙 | SDK·서버 |
| [05-testing.md](05-testing.md) | 개발용 키·라이선스 만들기, 검증 스크립트, **SDK 적용 후 수용 기준** | SDK·QA |
| [06-field-operations.md](06-field-operations.md) | 현장 설치, 오류별 조치, 알려진 한계 | 현장 |
| [RELEASE_MANIFEST.md](RELEASE_MANIFEST.md) | 이 패키지의 발행 목록·절차·검증 기준 | 발행 담당 |

## 3. 패키지 구성

```
CMakeLists.txt                     빌드 진입점 (서브프로젝트로 넣으면 라이브러리만 빌드)
cmake/SttLicenseHardening.cmake    stt_license_harden_shared_library()
core/include/stt_license/license.h 공개 헤더 (C++11 이상에서 포함 가능) — SDK 가 쓰는 유일한 헤더
core/src/                          구현 (C++17), embedded_key.cpp.in = 공개키 내장 템플릿
core/tests/, core/testdata/        단위 테스트(GoogleTest), 골든 벡터, JDK 교차검증
core/fuzz/                         libFuzzer 대상
tools/licensectl/                  개발·검증용 CLI (keygen / issue / verify / sign / pubkey)
integration/harness/               JNI 참조 구현(★ 표시 = SDK 로 옮길 부분) + Java 결합 테스트
integration/cmake-consumer/        SDK CMake 를 흉내 낸 소비자 프로젝트(연동 인수 테스트)
scripts/check.sh                   전체 검증 (7단계)
docs/sdk/                          이 문서들
README.md, VERSION, SHA256SUMS    (발행 시 생성) 루트 안내, 버전·원 커밋, 파일 해시
```

## 4. 요구 사항과 검증된 환경

| 항목 | 요구 |
|---|---|
| CMake | 3.16 이상 |
| 컴파일러 | C++17 (GCC 9 이상 또는 Clang). **SDK 코드는 C++11 이상**이면 공개 헤더를 포함할 수 있음 |
| 암호 | OpenSSL 1.1.1 이상의 libcrypto — **SDK가 이미 쓰는 것을 재사용**(01-build.md §5) |
| 플랫폼 | Linux(ELF). 심볼 은닉은 GNU ld / gold / lld |
| 배포 빌드 | 운영 이미지와 같은 **Ubuntu 22.04(glibc 2.35)** |

검증 이력:
- Ubuntu 20.04(gcc 9.4, CMake 3.16, OpenSSL 1.1.1f), 22.04(gcc 11.4, OpenSSL 3.0.2), 24.04(gcc 12/13, OpenSSL 3.0.13)
- 부모 프로젝트 C++14/C++20
- 공개 헤더 C++11~20 `-Wpedantic -Werror`

## 5. 정책 결정 현황 (2026-09-30)

| 항목 | 상태 |
|---|---|
| 코어의 책임 | **서명 검증은 공통.** 값(`allowed_channels`, `validity` 등) 사용·판정은 SDK가 결정 |
| 검증 수준 | `signature`(기본, 기존 SDK와의 호환 장치 — SDK의 별도 만료 장치 적용) / `full`(표준 필드·기간까지 코어 규칙으로). 설정 키 예 `--license-verify-mode` |
| 라이선스 갱신 방식 | **미정(추후 결정)** — 세션·채널 수 반영 등 분석 필요(02-integration.md §8). 현재는 서버 재시작 |
| 채널 의미 | `offline_stt` = sonastt-offline + whisper-offline **동시 사용 합계**, `online_stt` = 향후 스트리밍 엔진용 |
| 기존 `.key` | 새 모듈로는 검증되지 않음(1003). 공존하려면 SDK가 기존 경로로 분기(02-integration.md §6) |

## 6. 변경 이력

| 버전 | 내용 |
|---|---|
| 1.1.1 | 부모가 `STT_LICENSE_PUBLIC_KEY_FILE`·`STT_LICENSE_CRYPTO_TARGET`을 **일반 변수**로 지정하면 서브프로젝트에서 지워지던 문제 수정(CMP0126). 공개키 주입 방법론 문서화(01-build.md §6), 헤드리스 JDK에서 JNI 하네스 빌드 수정 |
| 1.1.0 | 서명 전용 API(`VerifySignature`, `VerifiedLicense`), 값 검증 포함 `VerifyLicense`, 모드 선택 `Verify(VerifyMode)`, `Utf16ToUtf8`(JNI 문자열), `ParseStandardFields`/`StateAt`. CMake 서브프로젝트 지원, `stt_license_harden_shared_library()`, 공개 헤더 C++11 호환, 비밀키 오지정 차단, FIFO 경로 무기한 대기 수정 |
| 1.0.0 | 파일 경로 기반 `Manager`(기간·채널 한도·재적재), 발급 CLI `licensectl` |

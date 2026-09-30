# 01. 빌드에 넣기 — SDK CMake 연동

STT SDK의 기존 CMake 빌드에 `stt_license`를 서브프로젝트로 넣어 JNI 공유 라이브러리(`libsonastt_jni_v2.so`)에 정적 링크하는 방법이다.
아래 절차는 `integration/cmake-consumer/`(SDK를 흉내 낸 CMake 프로젝트)에서 다음 조합으로 빌드·실행해 검증했다(`scripts/check.sh` 7단계). 코드 결합은 [02-integration.md](02-integration.md)에 있다.
- 부모 프로젝트의 C++ 표준: 14 / 20
- OpenSSL을 제공하는 방식: 3가지
- 빌드 환경: Ubuntu 20.04(CMake 3.16, gcc 9, OpenSSL 1.1.1f), 22.04(CMake 3.22, gcc 11, OpenSSL 3.0.2), 24.04

## 1. 최소 적용

```cmake
# SDK 의 CMakeLists.txt — JNI 라이브러리 타깃을 정의한 뒤
set(STT_LICENSE_PUBLIC_KEY_FILE ${CMAKE_SOURCE_DIR}/keys/stt_license_public.key CACHE FILEPATH "")
add_subdirectory(third_party/stt-license-sdk EXCLUDE_FROM_ALL)
target_link_libraries(sonastt_jni_v2 PRIVATE stt_license::embedded)
stt_license_harden_shared_library(sonastt_jni_v2)       # 필수 보안 조치 (§4)
```

- `EXCLUDE_FROM_ALL`을 붙이면 SDK가 링크하는 타깃만 빌드된다. 붙이지 않아도, 서브프로젝트로 넣었을 때는 테스트·`licensectl`·`-Werror`가 기본으로 꺼진다.
- 코어는 부모의 `CMAKE_CXX_STANDARD`와 상관없이 **최소 C++17**로 빌드된다. 공개 헤더 `stt_license/license.h`는 **C++11 이상**에서 포함할 수 있다. SDK 코드가 C++14여도 된다.
- 필요 조건: CMake 3.16 이상, GCC/Clang, OpenSSL 1.1.1 이상의 libcrypto.

## 2. 소스를 가져오는 방법

발행물은 `stt-license-sdk-<버전>.tar.gz`와 그 `.sha256` 파일이다([RELEASE_MANIFEST.md](RELEASE_MANIFEST.md)).

| 방법 | 예 | 비고 |
|---|---|---|
| 압축본을 SDK 저장소에 풀어 넣기 (권장) | `third_party/stt-license-sdk/`에 풀고 `add_subdirectory(third_party/stt-license-sdk EXCLUDE_FROM_ALL)` | 폐쇄망 빌드에도 그대로 쓸 수 있음. 버전은 폴더 안 `VERSION` 파일로 확인 |
| FetchContent (사내 파일 서버·아티팩트 저장소) | 아래 예 | `URL_HASH`로 무결성 고정 |
| 최소 복사 | `CMakeLists.txt`, `cmake/`, `core/include`, `core/src` 네 가지만 | 테스트·도구 없이도 빌드됨(서브프로젝트일 때 기본 OFF) |

```cmake
include(FetchContent)
FetchContent_Declare(stt_license
  URL      https://<사내 저장소>/stt-license-sdk-1.1.0.tar.gz
  URL_HASH SHA256=<.sha256 파일의 값>)
FetchContent_MakeAvailable(stt_license)
```

## 3. 옵션

| 캐시 변수 | 기본값(서브프로젝트) | 설명 |
|---|---|---|
| `STT_LICENSE_PUBLIC_KEY_FILE` | (없음) | 발급 담당이 준 `public.key`(Base64 44자). **`stt_license::embedded`를 쓰려면 필수** |
| `STT_LICENSE_CRYPTO_TARGET` | (없음) | SDK가 이미 가진 libcrypto 타깃 이름(§5) |
| `STT_LICENSE_OPENSSL_STATIC` | OFF | 이 프로젝트가 직접 `find_package(OpenSSL)` 할 때 정적 libcrypto 선호 |
| `STT_LICENSE_BUILD_TESTS` / `_TOOLS` | OFF (단독 빌드는 ON) | 단위 테스트 / `licensectl` |
| `STT_LICENSE_WERROR` | OFF (단독 빌드는 ON) | 경고를 에러로 |
| `STT_LICENSE_BUILD_FUZZ` / `_HARNESS` | OFF | 퍼저 / JNI 하네스 |

| 타깃 | 용도 |
|---|---|
| `stt_license::embedded` | **SDK가 링크하는 타깃.** 코어와 내장 공개키를 함께 포함(`VerifySignature`·`VerifyLicense`·`Verify`·`Manager::Open`이 여기서 정의됨) |
| `stt_license::core` | 공개키 없는 코어(발급 도구·테스트용). SDK는 쓰지 않는다 |

키 파일 없이 구성해도 설정 단계는 통과한다. 이때 `stt_license::embedded`를 빌드하려 하면 다음 메시지로 실패한다. 키를 빠뜨린 배포 빌드가 만들어지는 일을 막기 위한 의도된 동작이다.
```
#error "stt_license: STT_LICENSE_PUBLIC_KEY_FILE is not set. Configure with -DSTT_LICENSE_PUBLIC_KEY_FILE=<public.key>"
```
공개키는 비밀이 아니므로 SDK 저장소에 커밋해도 된다. **비밀키(`private.key`)는 절대 넣지 않는다.**
비밀키 파일은 `stt-license-ed25519-private-v1:`로 시작한다. 이 파일을 `STT_LICENSE_PUBLIC_KEY_FILE`에 잘못 지정하면 구성 단계가 `... is a PRIVATE key`로 중단된다. 비밀키가 SDK 바이너리에 내장되어 고객에게 배포되는 사고를 막기 위한 장치다.

## 4. 심볼 은닉 — `stt_license_harden_shared_library()`

OpenSSL을 정적 링크해도 그 심볼을 `.so` 밖으로 export하면, `LD_PRELOAD`로 `EVP_DigestVerify`를 바꿔치기해 **변조된 라이선스를 통과시킬 수 있다**(`scripts/check.sh` 6단계 대조군 2로 실험). 심볼을 기본값대로 export하는 빌드라면 이 함수가 필수다.

```cmake
stt_license_harden_shared_library(sonastt_jni_v2)                      # Java_*, JNI_OnLoad, JNI_OnUnload 만 공개
stt_license_harden_shared_library(sonastt_jni_v2 EXPORTS "sona_api_*") # C API 등 더 공개할 심볼 패턴
```
- 동작: 링커 버전 스크립트(`local: *`)와 `--exclude-libs,ALL`을 적용한다. 버전 스크립트는 빌드 디렉터리에 `<타깃>.exports.map`으로 생성된다.
- 대상: SHARED/MODULE 라이브러리만. Linux(ELF) 전용이며, 다른 플랫폼에서는 경고만 내고 아무것도 바꾸지 않는다.
- **적용 전 확인:** 다른 네이티브 라이브러리가 SDK `.so`의 심볼을 직접 쓰고 있다면, 그 패턴을 `EXPORTS`에 넣어야 한다. SDK가 다른 라이브러리를 사용하기만 하고 자기 심볼을 제공하지 않는다면 해당 없다.
- 적용 후 확인:
  ```bash
  nm -D --defined-only libsonastt_jni_v2.so | awk '{print $3}' | grep -v '^Java_'   # 출력이 없어야 함
  ```
- 이 함수가 막는 것은 **OpenSSL 가로채기**다. libc의 시간 함수는 여전히 가로챌 수 있다([06-field-operations.md](06-field-operations.md) §4).

## 5. OpenSSL 선택 규칙 — 한 `.so`에 OpenSSL이 두 벌 들어가면 안 된다

SDK는 이미 OpenSSL 1.1.1을 정적 링크하고 있다. `stt_license`가 **다른** libcrypto를 따로 찾아 링크하면, 같은 이름의 심볼 두 벌이 한 라이브러리에 섞여 링크 에러가 나거나 어느 쪽이 쓰일지 알 수 없게 된다. `stt_license`는 다음 순서로 libcrypto를 정한다. 구성 로그의 `stt_license:` 줄로 어느 경로가 쓰였는지 확인할 수 있다.

| 순서 | 조건 | 구성 로그 |
|---|---|---|
| 1 | `STT_LICENSE_CRYPTO_TARGET`을 지정함 | `stt_license: libcrypto from target <이름>` |
| 2 | 상위 디렉터리에서 이미 `find_package(OpenSSL)`를 호출해 `OpenSSL::Crypto`가 보임 | `stt_license: reusing OpenSSL::Crypto from parent project (...)` |
| 3 | 그 외 | `stt_license: OpenSSL <버전>: <경로>` (직접 `find_package`) |

SDK 상황별 설정:
- **SDK가 `find_package(OpenSSL)`를 쓰는 경우:** 그 호출을 `add_subdirectory(stt-license-sdk)`보다 **앞에, 같은 디렉터리나 상위 디렉터리에서** 한다. 형제 디렉터리에서 찾은 imported 타깃은 보이지 않는다.
- **SDK가 OpenSSL을 직접 빌드하거나 imported 타깃으로 갖고 있는 경우:**
  ```cmake
  set(STT_LICENSE_CRYPTO_TARGET sdk_openssl_crypto CACHE STRING "")   # SDK 의 libcrypto 타깃 이름
  add_subdirectory(third_party/stt-license-sdk EXCLUDE_FROM_ALL)
  ```
  그 타깃이 include 경로(`INTERFACE_INCLUDE_DIRECTORIES`)와, 정적 링크라면 `dl`·`pthread` 의존성까지 제공해야 한다. OpenSSL 1.1.1 미만이면 `crypto.cpp`의 `#error`로 컴파일이 멈춘다.
- **3번 경로가 쓰였는데 SDK의 OpenSSL과 다른 경로가 찍혔다면 잘못된 구성이다.** 1번이나 2번 방식으로 바꾼다.

## 6. 공개키 관리

- 공개키는 비밀이 아니다. SDK 저장소에 커밋해 빌드마다 같은 키를 쓰게 한다(예: `keys/stt_license_public.key`).
- 발급 담당에게서 받은 키의 **지문(SHA-256)**을 발급 서버 화면의 "공개키 지문"과 대조한다.
  ```bash
  base64 -d keys/stt_license_public.key | sha256sum
  ```
- 개발·테스트용 키(`licensectl keygen`으로 직접 만든 것)로 빌드한 `.so`를 **배포하지 않는다.** 그 키의 비밀키를 가진 사람은 누구나 라이선스를 만들 수 있다. 배포 빌드는 운영 공개키로만 한다([05-testing.md](05-testing.md) §1).
- 공개키가 바뀌면(키 유출·분실 대응) SDK를 다시 빌드해 배포해야 한다. 이전 키로 서명된 라이선스는 새 빌드에서 모두 `1004`가 된다.

## 7. 빌드 환경 주의

- 배포 `.so`는 운영 이미지와 같은 배포판(**Ubuntu 22.04, glibc 2.35**)에서 빌드한다. 더 새 glibc로 빌드하면 운영 이미지에서 로드되지 않는다.
- 코어는 스레드를 만들지 않고, 시그널 핸들러를 등록하지 않으며, 전역 초기화 순서에 의존하지 않는다.
- (`Manager` 방식에서) 라이선스 경로는 **일반 파일**이어야 한다. 디렉터리·FIFO·장치 파일은 `LICENSE_FILE_NOT_FOUND`로 즉시 거부된다(읽기가 멈추지 않음).

## 8. 문제 해결

| 증상 | 원인 | 조치 |
|---|---|---|
| `#error "stt_license: STT_LICENSE_PUBLIC_KEY_FILE is not set..."` | 키 파일 미지정 | `-DSTT_LICENSE_PUBLIC_KEY_FILE=<public.key>` |
| `... is a PRIVATE key. Use public.key` | 비밀키를 공개키 자리에 지정함 | `public.key`로 바꾸고, SDK 저장소·빌드 서버에 비밀키 사본이 남았는지 확인해 삭제 |
| `...: not a Base64 Ed25519 public key (44 chars)` | 키 파일 손상 또는 다른 파일 | 발급 담당이 준 `public.key` 사용 |
| `STT_LICENSE_CRYPTO_TARGET='x' is not a target visible here` | 타깃을 `add_subdirectory` 뒤에 정의했거나 형제 디렉터리에 정의함 | 타깃을 먼저, 같은 디렉터리나 상위 디렉터리에서 정의 |
| `Could NOT find OpenSSL` | 3번 경로인데 개발 패키지가 없음 | SDK의 OpenSSL을 1번 또는 2번 방식으로 넘김 |
| `#error "stt_license requires OpenSSL 1.1.1 or later"` | 너무 오래된 OpenSSL | OpenSSL 1.1.1 이상 |
| `multiple definition of 'EVP_...'` 등 OpenSSL 심볼 충돌 | libcrypto가 두 벌 링크됨 | §5 |
| `must be a SHARED or MODULE library` | 정적 라이브러리에 하드닝 함수를 적용함 | 최종 `.so` 타깃에 적용 |
| 하드닝 후 다른 라이브러리에서 `undefined symbol` | 필요한 심볼까지 숨겨짐 | `EXPORTS "<패턴>"` 추가 |
| 운영 이미지에서 `GLIBC_2.3x not found` | 더 새로운 배포판에서 빌드함 | Ubuntu 22.04에서 빌드 |
| 링크 에러 `undefined reference to stt::license::VerifySignature`(또는 `Manager::Open`) | `stt_license::core`만 링크함 | `stt_license::embedded`를 링크 |

## 9. CMake를 쓰지 않는 경우

1. `core/src/{json,crypto,license,manager}.cpp`를 `-std=c++17 -fPIC -fvisibility=hidden`으로 컴파일한다.
2. `core/src/embedded_key.cpp.in`의 `@STT_LICENSE_PUBLIC_KEY_B64@`를 `public.key` 내용(44자)으로 바꾼 파일도 함께 컴파일한다(내장 키를 쓰는 `VerifySignature`·`VerifyLicense`·`Verify`·`Manager::Open`이 이 파일에 있다).
3. include 경로는 `core/include`(공개)와 `core/src`(내부)다.
4. 최종 `.so`의 링크 옵션에 `-Wl,--exclude-libs,ALL -Wl,--version-script=<map>`을 추가한다. map의 형식은 §4 함수가 생성하는 파일과 같다.

# SDK 개발자 매뉴얼 — 라이선스 라이브러리(stt_license) 적용

이 매뉴얼은 STT SDK(C++/JNI)에 라이선스 검증을 **처음 넣는 개발자**를 위한 안내서입니다. 순서대로 따라 하면 패키지를 받는 데서 시작해 개발 키로 시험하고, 운영 키로 빌드해 배포 전 확인을 마치는 데까지 갈 수 있습니다.

- 대상: STT SDK(`libsonastt_jni_v2.so`) C++/JNI 개발자. C++와 CMake는 다룰 줄 알고, 전자 서명은 처음이라고 가정합니다.
- 기준: `stt_license` 3.0.0
- 이 문서는 **길잡이**입니다. 규칙의 원본은 번호 붙은 참조 문서(01~06)입니다. 둘이 다르면 참조 문서가 맞습니다.

| 하고 싶은 일 | 갈 곳 |
|---|---|
| 전체 그림부터 | §1 |
| 바로 따라 하기 | §3 (10단계) |
| API만 빨리 찾기 | §5 |
| 오류 코드 뜻 | §6 |
| 흔한 실수 피하기 | §7 |

---

## 1. 큰 그림

### 1.1 무엇을 하는 라이브러리인가
STT 제품은 인터넷이 없는 고객 현장에서 돕니다. 그래서 "이 고객이 정당하게 몇 채널을 쓸 수 있는가"를 서버에 묻지 않고 **SDK가 스스로** 판단해야 합니다. `stt_license`가 그 판단을 합니다.

```
 사내                                     고객 현장 (폐쇄망)
┌──────────────┐  license.lic  ┌───────────────────┐  문자열  ┌────────────────────────────┐
│ 발급 서버      │ ───────────▶ │ STT 서버 (Java)    │ ──────▶ │ STT SDK (C++/JNI)           │
│ 비밀키로 서명   │   반입(USB 등) │ 파일을 읽어 그대로 전달 │         │  stt_license: 내장 공개키로 검증 │
└──────────────┘               └───────────────────┘         │  통과 → 채널 상한만큼 세션 생성  │
      │ public.key (빌드 때 1회)                              │  실패 → 1001~1011 반환, 기동 중단│
      └─────────────────────────────────────────────────▶ └────────────────────────────┘
```

**비유:** 라이선스 파일은 회사 도장이 찍힌 계약서이고, SDK는 그 도장 모양을 외우고 있는 직원입니다. 도장이 맞지 않거나(위조·변조) 계약 기간이 지났으면 일을 시작하지 않습니다.
- 비유가 깨지는 곳: 도장 모양(공개키)을 알아도 도장(비밀키)을 새길 수 없습니다. 그래서 공개키는 숨길 필요가 없고, 대신 **바꿔치기되지 않게** 지킵니다.

### 1.2 SDK 개발자가 맡는 일과 맡지 않는 일
| SDK 개발자가 함 | 라이브러리가 함 | 하지 않음 |
|---|---|---|
| CMake에 넣고 공개키 지정 | 서명 검증(Ed25519) | 라이선스 발급(발급 담당) |
| `connect`에서 판정 함수 호출 | JSON 엄격 해석, 필드 범위 검사 | 비밀키 보관(받지 않음) |
| 판정 결과(채널 상한)를 세션 풀에 적용 | 기간 판정(시작 전·만료) | 판정 기준 바꾸기(설정으로 못 낮춤) |
| 오류 코드 반환과 로그 | 오류 이름·조치 문구 제공 | 서버 통신(없음) |

### 1.3 용어
| 용어 | 뜻 |
|---|---|
| `license.lic` | 서명된 평문 JSON. 발급 서버가 만듭니다. 형식: [04-license-format.md](04-license-format.md) |
| 공개키 `public.key` | 한 줄 44자 Base64. 빌드할 때 `.so` 안에 새겨 넣습니다. 실행 중에는 바꿀 수 없습니다 |
| 비밀키 `private.key` | `stt-license-ed25519-private-v1:`로 시작. **SDK 쪽에는 절대 두지 않습니다.** 개발 키만 예외(§3.3) |
| 지문 | 공개키 32바이트의 SHA-256. 키가 바꿔치기되지 않았는지 대조할 때 씁니다 |
| 판정 | 서명 + 표준 필드 + 기간을 모두 확인하는 것. `VerifyLicense` 한 번이 전부 합니다 |
| 채널 | 동시 STT 세션 수. `total`(전체 상한)과 항목별 세부 항목 상한(예: `offline.sonastt`) |
| `check_mode` | 라이브러리가 실제로 적용한 판정 수준(`full`/`minimal`). §4.4 |
| 개발 키 | 시험용으로 직접 만든 키쌍. 이 키로 빌드한 SDK는 **배포하지 않습니다** |

### 1.4 두 가지 사용 방식
| 방식 | 언제 | 핵심 함수 |
|---|---|---|
| **(A) 문자열 판정 — 기본** | STT 서버가 파일 내용을 문자열로 넘기는 지금 구조 | `VerifyLicense` |
| (B) 파일 경로 + 정책 내장 | SDK가 파일 경로를 직접 알고, 채널 점유·재적재까지 맡기고 싶을 때 | `Manager` |

이 매뉴얼은 (A)를 따라갑니다. (B)는 [02-integration.md](02-integration.md) 부록 A에 있습니다.

---

## 2. 준비물

### 2.1 받는 것 (세 가지, 서로 다른 경로로)
| 받는 것 | 경로 | 받은 뒤 확인 |
|---|---|---|
| `stt-license-sdk-3.0.0.tar.gz`와 `.sha256` | 소스 패키지 | `sha256sum -c stt-license-sdk-3.0.0.tar.gz.sha256` → `OK` |
| 운영 공개키 `public.key` | 패키지와 **별도** | 아래 지문 대조 |
| 운영 공개키 **지문** | **또 다른 경로**(사내 문서, 구두 확인 등. 키 파일과 같은 메일·메신저는 안 됨) | — |

```bash
base64 -d public.key | sha256sum     # 이 값이 발급 담당이 알려 준 지문과 한 글자도 다르지 않아야 함
```
- 왜 경로를 나누는가: 공개키는 비밀이 아니지만, 전달 중에 누군가 **자기 공개키로 바꿔 끼우면** 그 사람이 만든 라이선스를 SDK가 진짜로 받아들입니다. 한 경로가 뚫려도 다른 경로의 지문이 맞지 않아 드러나게 하려는 것입니다.

### 2.2 빌드 환경
| 항목 | 요구 |
|---|---|
| OS | Linux. **배포 빌드는 Ubuntu 22.04(glibc 2.35)** — 운영 이미지와 같아야 함 |
| CMake | 3.16 이상 |
| 컴파일러 | GCC 9 이상 또는 Clang. 라이브러리는 C++17로 빌드되고, SDK 코드는 C++11 이상이면 됨 |
| OpenSSL | 1.1.1 이상 libcrypto. **SDK가 이미 쓰는 것을 재사용** |
| 패키지 자체 검증용(선택) | Ninja, GoogleTest, JDK 17+, Python 3, `pkg-config`, 정적 `libcrypto.a`(`libssl-dev`) |

- WSL을 쓴다면 `/mnt/c` 아래가 아니라 리눅스 파일시스템(예: `~/work`)에서 작업합니다. `/mnt/c`에서는 파일 권한이 적용되지 않아 일부 검사가 건너뛰어집니다.

---

## 3. 따라 하기 — 10단계

각 단계 끝의 **확인**이 통과해야 다음 단계로 갑니다.

### 3.1 패키지 풀고 버전 확인
```bash
sha256sum -c stt-license-sdk-3.0.0.tar.gz.sha256
mkdir -p third_party && tar -xzf stt-license-sdk-3.0.0.tar.gz -C third_party
mv third_party/stt-license-sdk-3.0.0 third_party/stt-license-sdk      # 경로를 버전과 무관하게 고정
head -1 third_party/stt-license-sdk/VERSION
```
- SDK 저장소 안 `third_party/stt-license-sdk/`에 넣는 방식을 권장합니다. 폐쇄망 빌드에서도 그대로 쓸 수 있습니다. 다른 방법(FetchContent, 최소 복사)은 [01-build.md](01-build.md) §2에 있습니다.
- **확인:** `VERSION` 첫 줄이 `3.0.0`.

### 3.2 (선택이지만 권장) 패키지 자체 검증
```bash
cd third_party/stt-license-sdk
scripts/check.sh        # 수 분. 마지막 줄이 "ALL CHECKS PASSED"
```
- 이 스크립트는 패키지만으로 단위 테스트, JNI 결합 시험, 공격 시험, CMake 연동 시험까지 7단계를 돌립니다. 내 빌드 환경에서 라이브러리가 제대로 동작하는지 SDK에 넣기 **전에** 알 수 있습니다.
- 도구가 없어 실패하면 [05-testing.md](05-testing.md) §2의 준비물을 설치합니다.
- **확인:** `ALL CHECKS PASSED`.

### 3.3 개발 키와 시험용 라이선스 만들기
운영 비밀키는 발급 서버에만 있으므로, 시험에는 **직접 만든 개발 키**를 씁니다. 패키지의 `licensectl`로 만듭니다.

```bash
cd third_party/stt-license-sdk
cmake -S . -B build -G Ninja && cmake --build build --target licensectl
./build/licensectl keygen -out build/dev-keys          # build/dev-keys/public.key, private.key
./build/licensectl issue -key build/dev-keys/private.key \
    -in core/testdata/contract.example.json -out build/dev.lic
./build/licensectl verify -pub build/dev-keys/public.key build/dev.lic
```
실제 출력 예(값은 실행마다 다름):
```
SIGNATURE OK
check_mode     : full
license_ver    : 1
license_id     : b739e041f727291078fe7ae150eae9d9
project_name   : STT Platform Modernization
license_type   : production
solution       : sonastt-universal
site_id        : SEOUL-MAIN-DC
channels total : 16
  offline      : sonaasr=4 sonaml=4 sonastt=8
  online       : sonaasr=2 sonastt=8
valid          : 2026-09-21T00:00:00Z ~ 9999-12-31T23:59:59Z
state at 2026-10-23T05:12:06Z: VALID
```
- `verify`의 종료 코드: **0 = 서명 OK이고 지금 유효**, 1 = 그 밖(서명 실패, 만료 등). 만료된 파일도 서명이 맞으면 `SIGNATURE OK`가 찍히지만 마지막 줄이 `EXPIRED`이고 종료 코드는 1입니다.
- `-at YYYY-MM-DDTHH:MM:SSZ`를 붙이면 그 시각 기준으로 판정합니다(만료 시험에 편리). `-json`은 한 줄 JSON으로 출력합니다.
- 개발 키와 라이선스는 `build/` 아래에 두고 **커밋하지 않습니다.** `private.key`는 SDK 저장소에 들어가면 안 됩니다.
- **확인:** 마지막 줄 `VALID`, 종료 코드 0.

**시험용 라이선스 더 만들기** — 계약 JSON을 고쳐 `issue`합니다.
```bash
# 만료된 라이선스 (trial 은 실제 종료일이 필요, 1회 180일 이하)
python3 - <<'PY'
import json
c = json.load(open("core/testdata/contract.example.json"))
c["license_type"] = "trial"
c["validity"] = {"not_before": "2026-01-01T00:00:00Z", "not_after": "2026-03-01T00:00:00Z"}
json.dump(c, open("build/expired.json", "w"))
PY
./build/licensectl issue -key build/dev-keys/private.key -in build/expired.json -out build/expired.lic

# 변조된 라이선스 (서명 뒤 숫자 하나 바꿈)
sed 's/"total": 16/"total": 17/' build/dev.lic > build/tampered.lic
./build/licensectl verify -pub build/dev-keys/public.key build/tampered.lic
#   → INVALID LICENSE_BAD_SIGNATURE: signature does not match license contents
```
- 시작 전 라이선스는 `not_before`를 미래로 둔 `trial`로 만듭니다.
- 운영(`production`)은 발급 정책상 종료가 `9999-12-31T23:59:59Z`(영구)여야 합니다. 만료 시험에는 `trial`을 씁니다.

### 3.4 SDK CMake에 넣기
SDK의 `CMakeLists.txt`에서 **JNI 라이브러리 타깃을 정의한 뒤**에 넣습니다.

```cmake
# 운영 공개키는 SDK 저장소 파일로 고정. 개발 키는 명시적 옵션으로만, 경고와 함께.
option(SDK_LICENSE_DEV_KEY "개발용 공개키로 빌드 (배포 금지)" OFF)
if(SDK_LICENSE_DEV_KEY)
  set(STT_LICENSE_PUBLIC_KEY_FILE ${SDK_LICENSE_DEV_KEY_FILE})
  message(WARNING "stt_license: DEV public key ${SDK_LICENSE_DEV_KEY_FILE} — 이 빌드는 배포하지 말 것")
else()
  set(STT_LICENSE_PUBLIC_KEY_FILE ${CMAKE_SOURCE_DIR}/keys/stt_license_public.key)
endif()

add_subdirectory(third_party/stt-license-sdk EXCLUDE_FROM_ALL)
target_link_libraries(sonastt_jni_v2 PRIVATE stt_license::embedded)
stt_license_harden_shared_library(sonastt_jni_v2)     # 필수: JNI 진입점(Java_*) 밖의 심볼을 숨김
```
한 줄씩 보면 이렇습니다.

| 줄 | 하는 일 | 빼면 |
|---|---|---|
| `set(STT_LICENSE_PUBLIC_KEY_FILE …)` | 내장할 공개키 파일. **일반 변수 + 절대 경로**로 지정 | `#error "... STT_LICENSE_PUBLIC_KEY_FILE is not set"`으로 빌드 실패(키 없는 바이너리를 막는 의도된 동작) |
| `add_subdirectory(… EXCLUDE_FROM_ALL)` | 라이브러리를 서브프로젝트로 추가. 테스트·도구는 자동으로 꺼짐 | — |
| `stt_license::embedded` | 코어 + 내장 공개키. **SDK가 링크하는 유일한 타깃** | `stt_license::core`만 링크하면 `undefined reference to stt::license::VerifyLicense` |
| `stt_license_harden_shared_library` | `.so`의 내보내기 심볼을 JNI 진입점으로 제한 | 공격자가 내부 검증 함수를 바꿔치기할 여지가 생김(§7) |

- **OpenSSL:** SDK가 `find_package(OpenSSL)`를 쓰면 그 호출을 `add_subdirectory`보다 **앞에** 둡니다. 그러면 라이브러리가 같은 OpenSSL을 재사용합니다. SDK가 OpenSSL을 직접 빌드한다면 `STT_LICENSE_CRYPTO_TARGET`으로 그 타깃을 넘깁니다. 한 `.so`에 OpenSSL이 두 벌 들어가면 안 됩니다([01-build.md](01-build.md) §5). 구성 로그의 `stt_license:` 줄로 어느 OpenSSL을 썼는지 확인합니다.
- **왜 일반 변수인가:** 일반 변수로 지정하면 명령줄 `-DSTT_LICENSE_PUBLIC_KEY_FILE=…`이 덮어쓰지 못합니다. 실수로 개발 키가 운영 빌드에 섞이는 길을 막습니다. 자세한 비교는 [01-build.md](01-build.md) §6.3에 있습니다.
- 운영 공개키(§2.1에서 지문을 대조한 것)는 `keys/stt_license_public.key`로 **SDK 저장소에 커밋**합니다. 공개키는 비밀이 아니고, 커밋해 두면 누가 언제 바꿨는지 이력이 남습니다.

개발 키로 빌드하기:
```bash
cmake -S . -B build-dev -DSDK_LICENSE_DEV_KEY=ON \
      -DSDK_LICENSE_DEV_KEY_FILE=$PWD/third_party/stt-license-sdk/build/dev-keys/public.key
cmake --build build-dev
```
- **확인:** 구성 로그에 `DEV public key … 이 빌드는 배포하지 말 것` 경고가 나오고, 빌드가 성공함.

### 3.5 `connect`에서 판정하기
STT 서버는 `createSonaSTT(callback, configFile, hostLicense)`로 **파일 내용 문자열**을 넘깁니다. SDK의 native `connect`가 그 문자열을 판정합니다.

**① `jstring` → 표준 UTF-8 (가장 흔한 함정)**
```cpp
#include "stt_license/license.h"
namespace lic = stt::license;

// jstring → 표준 UTF-8. GetStringUTFChars 를 쓰지 않는다(변형 UTF-8 이라 서명한 바이트와 달라짐).
static lic::Error JStringToUtf8(JNIEnv* env, jstring js, std::string* out) {
  const jsize n = env->GetStringLength(js);
  if (n < 0) return lic::Error::Internal;
  if (static_cast<size_t>(n) > 64 * 1024) return lic::Error::FileTooLarge;   // 변환 전에 상한
  std::vector<jchar> buf(static_cast<size_t>(n));
  if (n > 0) env->GetStringRegion(js, 0, n, buf.data());
  if (env->ExceptionCheck()) return lic::Error::Internal;
  static_assert(sizeof(jchar) == sizeof(std::uint16_t), "jchar must be 16-bit");
  return lic::Utf16ToUtf8(reinterpret_cast<const std::uint16_t*>(buf.data()), buf.size(), out)
             ? lic::Error::Ok : lic::Error::ParseError;
}
```
- `GetStringUTFChars`는 이모지 같은 문자를 표준과 다른 바이트로 돌려줍니다. 그러면 정상 라이선스가 `1003`으로 떨어집니다. **ASCII와 한글만 있는 라이선스로는 이 문제가 드러나지 않으므로** 평소 시험으로는 못 잡습니다. 처음부터 위 함수를 씁니다.

**② 판정과 반환**
```cpp
JNIEXPORT jint JNICALL Java_<패키지>_<클래스>_connect(JNIEnv* env, jobject self, jobject callback,
                                                     jstring configFile, jstring hostLicense) {
  try {
    if (hostLicense == nullptr) return /* SDK 의 잘못된 인자 코드 */ 2;

    std::string text;
    lic::Error e = JStringToUtf8(env, hostLicense, &text);
    lic::VerifiedLicense license;
    lic::LicenseFields fields;            // 성공할 때만 채워진다
    std::string detail;                   // 실패 이유(로그용, 영문)
    if (e == lic::Error::Ok) e = lic::VerifyLicense(text, &license, &fields, &detail);

    if (e != lic::Error::Ok) {
      LOG_ERROR("license rejected: %s (%s) detail=%s lib=%s max_ver=%d",
                lic::ErrorName(e), lic::ErrorMessage(e), detail.c_str(),
                lic::LibraryVersion(), lic::MaxLicenseVer());
      return 1000 + static_cast<int>(e);  // 1001~1011: 연결 거부
    }
    LOG_INFO("license ok: id=%s check_mode=%s lib=%s max_ver=%d",
             fields.license_id.c_str(), lic::CheckModeName(fields.check_mode),
             lic::LibraryVersion(), lic::MaxLicenseVer());

    // ---- 여기부터 SDK 초기화 (§3.6) ----
    return 0;
  } catch (...) {
    return /* SDK 의 내부 오류 코드 */ 9;   // C++ 예외가 JVM 으로 넘어가면 프로세스가 죽는다
  }
}
```
`LOG_ERROR`·`LOG_INFO`는 SDK의 로그 함수로 바꿉니다. 지켜야 할 순서와 규칙은 다음과 같습니다.

| 규칙 | 이유 |
|---|---|
| **판정을 맨 앞에** 둡니다. 모델·GPU 메모리는 판정이 `Ok`일 때만 잡습니다 | 라이선스 없는 실행이 자원을 먼저 잡지 않게 |
| 판정 통과를 전역 상태로 **먼저** 남기지 않습니다. 초기화가 모두 끝난 뒤에 반영합니다 | 반쯤 초기화된 상태가 남으면 다음 호출이 판정 없이 지나갈 수 있음 |
| JNI 진입점은 전부 `try { … } catch (...)`로 감쌉니다 | 예외가 JVM으로 넘어가면 프로세스 종료 |
| 두 번째 `connect` 호출은 일단 **거부**합니다 | 재호출 정책(갱신 방식)은 아직 미정([02-integration.md](02-integration.md) §8) |
| SDK의 기존 오류 코드가 `1001~1011`과 겹치지 않는지 확인합니다 | 겹치면 STT 서버가 원인을 잘못 판단 |
| `callback`을 작업 스레드에서 쓰려면 `NewGlobalRef`를 씁니다 | 로컬 참조는 `connect`가 끝나면 무효 |

- 실행으로 검증한 참조 구현: 패키지의 `integration/harness/harness_jni.cpp` → `connectHostLicense`(★ 표시가 SDK로 옮길 부분). Java 쪽 시험은 `integration/harness/java/licenseharness/HarnessTest.java`입니다.
- **확인:** 개발 키 빌드 + `build/dev.lic`로 `connect`가 0을 반환하고, 로그에 `check_mode=full lib=3.0.0 max_ver=1`이 남음.

### 3.6 판정된 값으로 세션 풀 만들기
판정만 하고 값을 쓰지 않으면 채널 상한이 **집행되지 않습니다.** 판정은 "이 파일이 진짜인가"이고, 상한 적용은 SDK의 몫입니다.

```cpp
// fields 는 VerifyLicense 가 범위까지 판정한 값이다(다시 검사하지 않는다).
auto cap = [&](const std::string& group, const std::string& engine) {
  auto g = fields.channels.find(group);
  if (g == fields.channels.end()) return 0;            // 라이선스에 없는 항목 = 상한 0
  auto it = g->second.find(engine);
  return it == g->second.end() ? 0 : it->second;       // 라이선스에 없는 세부 항목 = 상한 0
};
const int sonastt_offline = std::min(cfg_num_sessions, cap("offline", "sonastt"));
// 동시 예약은 세부 항목 상한과 fields.total_channels(전체 상한)를 "모두" 지킨다.
```
- 예: `total` 16, `offline.sonastt` 8, `online.sonastt` 8이면, 각 엔진은 8까지, **둘을 합쳐서는 16까지**입니다. 세부 항목 상한의 합이 `total`보다 크거나 작을 수 있으므로 두 조건을 따로 셉니다.
- 항목·세부 항목 이름(`offline`, `sonastt` 등)은 라이선스마다 정할 수 있습니다. SDK가 어떤 이름을 읽을지 미리 정하고, 발급 담당과 같은 이름을 씁니다.
- **동작 판단에는 안정 필드만** 씁니다: `total_channels`, `channels`, `not_before`·`not_after`, `license_id`. `license_type`(운영/시험)이나 `project_name`으로 기능을 나누지 않습니다. 이 값들은 앞으로 나올 형식에서 비어 있을 수 있습니다(§4.4).
- 동시 예약 계수(점유·반납)를 직접 짜기 번거로우면 (B) `Manager`의 `Acquire`/`Release`를 참고합니다([02-integration.md](02-integration.md) 부록 A).

### 3.7 STT 서버(Java) 쪽 확인
SDK만 고쳐서는 끝나지 않습니다. 서버가 파일을 **바이트 그대로** 넘겨야 합니다.
```java
byte[] bytes = Files.readAllBytes(Path.of(licenseFilePath));   // readLine·trim 금지
String hostLicense = new String(bytes, StandardCharsets.UTF_8); // 기본 문자셋 금지
int rc = sonaStt.createSonaSTT(callback, configFile, hostLicense);
if (rc != 0) throw new IllegalStateException("STT license rejected: " + rc);
```
- 줄 단위로 읽거나 공백을 다듬으면 서명한 바이트와 달라져 `1004`가 됩니다. 서버 측 전체 규칙: [03-stt-server.md](03-stt-server.md).

### 3.8 시험 — 수용 기준 돌리기
개발 키 빌드로 아래를 확인합니다. 전체 목록은 [05-testing.md](05-testing.md) §3이고, **전 항목을 통과하기 전에는 배포하지 않습니다.**

| 입력 | 기대 반환 | 만드는 법 |
|---|---|---|
| 정상 라이선스 | 0 | §3.3 `dev.lic` |
| 숫자 한 글자 변조 | 1004 | §3.3 `tampered.lic` |
| 만료 | 1008 | §3.3 `expired.lic` |
| 시작 전 | 1007 | `not_before`가 미래인 `trial` |
| 다른 개발 키로 서명 | 1004 | `keygen`을 한 번 더 해서 그 키로 `issue` |
| `project_name`에 이모지 | 0 (`GetStringUTFChars`를 쓰면 1003) | 계약 JSON의 `project_name`에 이모지를 넣고 `issue` |
| 빈 문자열·JSON 아님 | 1003 | `""`, `"hello"` |
| 운영(영구) 라이선스 | 0 | `contract.example.json` 그대로 |
| 동시 세션이 세부 항목 상한 초과 | 거부(SDK 정책 또는 `1009`) | 상한 2짜리 라이선스로 세션 3개 |
| 세부 항목은 남았지만 합계가 `total` 초과 | 거부 | `total` 2, 세부 항목 2개 각 2 |

### 3.9 운영 빌드
1. 운영 공개키 파일 `keys/stt_license_public.key`의 지문이 발급 담당이 알려 준 지문과 같은지 **다시** 확인합니다.
2. **새 빌드 디렉터리**에서, `SDK_LICENSE_DEV_KEY=OFF`(기본값)로 빌드합니다. 한 번 개발 키를 쓴 빌드 디렉터리는 캐시에 그 키가 남을 수 있습니다.
3. Ubuntu 22.04에서 빌드합니다.

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release     # 새 디렉터리, DEV_KEY 옵션 없음
cmake --build build-release
```
- 운영 키로 빌드한 SDK에서는 개발 라이선스가 `1004`로 거부됩니다. 정상입니다. 운영 빌드의 동작 시험이 필요하면 발급 담당에게 시험용 운영 라이선스(`trial`)를 요청합니다.

### 3.10 배포 전 확인
```bash
SO=build-release/libsonastt_jni_v2.so
nm -D --defined-only $SO | awk '{print $3}' | grep -v '^Java_'          # 출력 없어야 함(심볼 은닉)
ldd $SO | grep libcrypto                                                 # 정적 OpenSSL이면 출력 없음
objdump -T $SO | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1            # GLIBC_2.35 이하
strings -n 44 $SO | grep -cxF "$(cat keys/stt_license_public.key)"      # 1 (운영 키가 들어감)
strings -n 44 $SO | grep -cxF "$(cat third_party/stt-license-sdk/build/dev-keys/public.key)"   # 0 (개발 키 없음)
```
- 키가 정말 들어갔는지 확인하는 4단계(구성 → 중간 산출물 → 최종 `.so` → 동작)는 [01-build.md](01-build.md) §6.5에 있습니다. `libstt_license.a`에는 키가 없으니(키는 `libstt_license_key.a`에 있음) **최종 `.so`로** 확인합니다.
- **확인:** 다섯 줄 모두 기대대로이고, §3.8 표를 통과함.

---

## 4. 라이선스 파일 이해하기

### 4.1 생김새
```json
{
  "license_ver": 1,
  "license_id": "b739e041f727291078fe7ae150eae9d9",
  "project_name": "STT Platform Modernization",
  "license_type": "production",
  "solution": "sonastt-universal",
  "site_id": "SEOUL-MAIN-DC",
  "allowed_channels": {
    "total": 16,
    "offline": { "sonastt": 8, "sonaml": 4, "sonaasr": 4 },
    "online":  { "sonastt": 8, "sonaasr": 2 }
  },
  "validity": {
    "issued_at": "2026-10-23T05:12:06Z",
    "not_before": "2026-09-21T00:00:00Z",
    "not_after": "9999-12-31T23:59:59Z"
  },
  "warning_notice": "This software license is geographically restricted ...",
  "signature": "(표준 Base64 88자)"
}
```

### 4.2 필드를 세 갈래로 나눠 보기
| 갈래 | 필드 | SDK가 할 일 |
|---|---|---|
| **집행에 쓰는 값(안정 필드)** | `allowed_channels`, `validity.not_before`·`not_after`, `license_id` | 상한 적용, 로그. 동작 판단은 이것만 |
| 표시용 값 | `project_name`, `license_type`, `solution`, `site_id`, `warning_notice`, `validity.issued_at` | 필요하면 로그·화면에. 동작을 나누지 않음 |
| 형식 정보 | `license_ver`, `check_mode`(있을 때), `signature` | 라이브러리가 처리. SDK는 신경 쓰지 않음 |

- 모든 시각은 UTC `YYYY-MM-DDTHH:MM:SSZ`입니다. `LicenseFields`에서는 UTC epoch 초로 받습니다.
- 운영 라이선스는 `not_after`가 `9999-12-31T23:59:59Z`(영구)입니다. 특별 취급하지 않고 그냥 아주 먼 시각으로 비교합니다.
- 기간 판정에는 **유예 기간이 없습니다**: `now < not_before` → 시작 전, `now ≤ not_after` → 유효, 그 밖 → 만료.

### 4.3 서명은 무엇을 덮는가
`signature`를 뺀 나머지 전체를 정해진 규칙(RFC 8785)으로 한 줄로 정규화한 바이트에 서명합니다. 그래서:
- 들여쓰기, 줄바꿈, 키 순서는 바꿔도 서명이 유지됩니다.
- **값**은 한 글자만 바뀌어도(숫자, 공백 포함 문자열) `1004`입니다.
- 표에 없는 키를 누가 추가해도 서명에 포함되므로, 서명 뒤에 끼워 넣으면 `1004`입니다.

### 4.4 `check_mode` — 앞으로 형식이 바뀌어도 이전 SDK가 받게 하는 장치
지금 발급하는 형식은 `license_ver` 1이고, 3.0.0 라이브러리는 이 형식을 압니다(`MaxLicenseVer()` = 1). 그래서 지금은 항상 `check_mode=full`입니다.
- 지금 발급되는 파일에는 `check_mode` 키가 없습니다. 정상입니다. 키가 없으면 `full`입니다.
- 로그에 남기는 `fields.check_mode`는 파일에 적힌 값이 아닙니다. 라이브러리가 실제로 적용한 수준입니다.

나중에 형식이 `license_ver` 2로 올라가면, 이미 현장에 있는 3.0.0 SDK는 그 형식을 모릅니다. 이때:
- 파일에 `check_mode`가 없으면: 모르는 형식이므로 `1006`으로 거부합니다(안전한 쪽).
- 파일에 `"check_mode": "minimal"`이 있으면: **안정 필드와 기간만** 판정해 받습니다. `fields.check_mode`가 `Minimal`이고 표시용 필드는 비어 있을 수 있습니다.

**SDK가 지금 해 둘 일은 두 가지뿐입니다.**
1. `connect` 로그에 `CheckModeName(fields.check_mode)`, `LibraryVersion()`, `MaxLicenseVer()`를 남깁니다. 현장에서 `minimal`이 보이면 "이 SDK는 업그레이드 대상"이라는 신호입니다.
2. 동작 판단에 안정 필드만 씁니다(§3.6). 그래야 `minimal` 파일에서도 그대로 동작합니다.

자세한 규칙: [04-license-format.md](04-license-format.md) §5.

---

## 5. API 빠른 참조

헤더는 `stt_license/license.h` 하나, 네임스페이스는 `stt::license`입니다. **모든 함수는 예외를 던지지 않고**(`std::bad_alloc` 제외) `Error`를 반환하며, **실패하면 출력 인자를 채우지 않습니다.**

| 함수 | 하는 일 |
|---|---|
| `Error VerifyLicense(text, &license, &fields, &detail)` | 판정 한 번에 전부: 크기(64 KiB) → BOM 제거 → 엄격 JSON → 서명 → `check_mode` → 표준 필드 → 기간(시스템 시각). `fields`·`detail`은 `nullptr` 가능 |
| `bool Utf16ToUtf8(ptr, len, &out)` | JNI UTF-16 → 표준 UTF-8. 짝 없는 서로게이트면 `false` |
| `State StateAt(fields, now_epoch)` | 이미 판정한 값으로 기간만 다시 봄(운영 중 재판정용) |
| `license.GetString/GetInt/GetBool(path, &out)`, `Has(path)` | 판정을 통과한 파일에서 표준 밖 값 읽기. 경로는 점 구분(`"allowed_channels.offline.sonastt"`) |
| `license.canonical_json()` | 서명 대상 바이트(정규 JSON) |
| `ErrorName(e)` / `ErrorMessage(e)` | `"LICENSE_BAD_SIGNATURE"` / 현장 조치 문구(영문) |
| `StateName(s)` / `CheckModeName(m)` | `"VALID"` 등 / `"full"`·`"minimal"` |
| `LibraryVersion()` / `MaxLicenseVer()` | `"3.0.0"` / `1` |

`LicenseFields`의 멤버: `check_mode`, `license_ver`, `license_id`, `project_name`, `license_type`, `solution`, `site_id`, `warning_notice`, `total_channels`, `channels`(항목 → 세부 항목 → 상한), `issued_at`, `not_before`, `not_after`.

- **스레드:** `VerifyLicense`는 상태가 없어 여러 스레드에서 동시에 불러도 됩니다. `VerifiedLicense`는 불변입니다.
- **자원:** 라이브러리는 스레드를 만들지 않고, 시그널 핸들러를 등록하지 않으며, 파일·환경 변수·설정을 읽지 않습니다(문자열 방식).
- 표 전체와 `Manager` API: [02-integration.md](02-integration.md) §2, 부록 A.

---

## 6. 오류 코드와 조치

`connect` 반환값 = `1000 + Error 순번`. 순번은 고정입니다(헤더의 `static_assert`).

| 코드 | 이름 | 흔한 원인 | 개발 중 확인할 것 |
|---|---|---|---|
| 1001 | `LICENSE_FILE_NOT_FOUND` | (Manager 방식) 파일 없음 | 경로, 권한 |
| 1002 | `LICENSE_FILE_TOO_LARGE` | 64 KiB 초과(로그 파일을 잘못 넘김 등) | 서버가 넘긴 파일이 맞는지 |
| 1003 | `LICENSE_PARSE_ERROR` | JSON 아님, 옛 `.key`, **`GetStringUTFChars` 사용** | §3.5 ① 변환 함수 |
| 1004 | `LICENSE_BAD_SIGNATURE` | 변조, **다른 키로 빌드한 SDK**(개발↔운영), 서버가 바이트를 바꿈 | 내장 키(§3.10), 서버 읽기 방식(§3.7) |
| 1005 | `LICENSE_INVALID_FIELD` | 필수 필드 누락, 범위·시각 형식 위반 | `detail` 문자열이 위치를 알려 줌 |
| 1006 | `LICENSE_UNSUPPORTED_VERSION` | 이 라이브러리가 모르는 형식, 모르는 `check_mode` 값 | 라이브러리 업그레이드 또는 호환 라이선스 |
| 1007 | `LICENSE_NOT_YET_VALID` | 시작 전, 또는 **호스트 시계가 틀림** | `date -u` |
| 1008 | `LICENSE_EXPIRED` | 종료 지남(유예 없음) | 갱신본 |
| 1009 | `LICENSE_CHANNEL_LIMIT` | (Manager·SDK 정책) 상한까지 사용 중 | 잠시 뒤 재시도 |
| 1010 | `LICENSE_INTERNAL_ERROR` | 내장 공개키 손상, 암호 라이브러리 실패 | 빌드 다시, OpenSSL 구성(§3.4) |
| 1011 | `LICENSE_ENGINE_NOT_LICENSED` | (Manager·SDK 정책) 라이선스에 없거나 상한 0인 엔진 | 항목 이름 철자 |

- `detail`은 개발자용 영문 설명입니다(예: `signature does not match license contents`). 로그에 함께 남기면 원인을 바로 찾을 수 있습니다.
- 현장 엔지니어용 조치표: [06-field-operations.md](06-field-operations.md).

---

## 7. 하지 말아야 할 것

| 하지 말 것 | 왜 |
|---|---|
| `GetStringUTFChars`로 라이선스 문자열 받기 | 변형 UTF-8이라 일부 문자에서 정상 라이선스가 1003 |
| 공개키를 설정 파일·환경 변수로 받게 만들기 | 고객이 자기 키쌍을 만들어 끼우면 라이선스를 직접 서명할 수 있음. 키는 빌드 때 고정이 설계 의도 |
| `private.key`를 SDK 저장소·빌드 서버에 두기 | 비밀키 오지정은 구성 단계에서 막지만(`... is a PRIVATE key`), 사본 자체가 유출 위험 |
| 개발 키 빌드를 배포 | 개발 키로 서명한 라이선스는 누구나 만들 수 있음 |
| 하드닝(`stt_license_harden_shared_library`) 생략 | `LD_PRELOAD` 등으로 검증 함수를 가로채는 공격에 열림. 패키지 `check.sh` 6단계가 이 공격을 실제로 시험함 |
| OpenSSL 두 벌 링크 | 심볼 충돌, 어느 쪽이 쓰일지 알 수 없음 |
| 판정 결과를 무시하고 기본 세션 수로 기동 | 채널 상한이 집행되지 않음 |
| 실패 코드를 다른 경로로 우회(예: `1004`면 옛 경로로) | 변조 파일이 우회로로 통과. 옛 `.key` 공존은 **`ParseError`일 때만** 분기([02-integration.md](02-integration.md) §6) |
| `license_type`으로 기능 나누기 | 표시용 필드라 앞으로의 형식에서 비어 있을 수 있음 |
| 서버에서 라이선스를 줄 단위로 읽거나 `trim` | 바이트가 바뀌어 1004 |

---

## 8. 자주 묻는 질문

**Q. 공개키가 공개돼도 괜찮나요?**
네. 공개키로는 서명을 확인만 할 수 있고 만들 수는 없습니다. 지켜야 할 것은 **바꿔치기**입니다(§2.1).

**Q. 서명만 확인하고 기간은 SDK가 따로 보고 싶습니다.**
3.0.0에서는 그런 방식이 없습니다. `VerifyLicense`가 기간까지 판정하고, 설정으로 낮출 수 없습니다. 이전 판의 `signature` 모드와 `--license-verify-mode`는 삭제됐습니다.

**Q. 운영 중에 라이선스 파일을 바꾸면 반영되나요?**
문자열 방식에서는 `connect` 때 한 번 판정하므로 **STT 서버 재시작**이 필요합니다. 운영 중 갱신 방식은 아직 정하지 않았습니다([02-integration.md](02-integration.md) §8). 장시간 도는 서버의 기간 재확인이 필요하면 `StateAt(fields, now)`를 쓸 수 있습니다.

**Q. 기존 `sonaLicense.*.key`도 받아야 합니다.**
새 라이브러리로는 검증되지 않습니다(1003). `VerifyLicense`가 `ParseError`를 돌려줄 때만 기존 경로로 분기합니다. NUL 바이트가 있으므로 (포인터, 길이)로 넘깁니다([02-integration.md](02-integration.md) §6).

**Q. 우리 SDK는 C++14입니다.**
괜찮습니다. 공개 헤더는 C++11 이상에서 포함할 수 있고, 라이브러리 자체는 C++17로 따로 빌드됩니다.

**Q. CMake를 쓰지 않습니다.**
소스 네 개와 내장 키 파일을 직접 컴파일하는 방법이 [01-build.md](01-build.md) §9에 있습니다.

**Q. 개발 라이선스가 운영 빌드에서 1004입니다.**
정상입니다. 운영 빌드는 운영 공개키만 압니다. 운영 빌드 시험용 라이선스는 발급 담당에게 요청합니다.

**Q. 라이브러리를 새 버전으로 올리려면?**
패키지를 `third_party/stt-license-sdk`에 덮어쓰고 [README.md](README.md) §6 변경 이력에서 호환이 깨지는 변경이 있는지 봅니다. 공개키가 그대로면 키 파일은 바꾸지 않습니다. 다시 빌드한 뒤 §3.8·§3.10을 반복합니다.

---

## 부록. 참조 문서 지도
| 문서 | 언제 보나 |
|---|---|
| [README.md](README.md) | 패키지 구성, 버전, 변경 이력 |
| [01-build.md](01-build.md) | CMake 옵션 전체, OpenSSL 규칙, 공개키 주입·확인, 빌드 문제 해결 |
| [02-integration.md](02-integration.md) | 코드 결합 규칙 원본, `Manager` 방식, 옛 `.key` 공존 |
| [03-stt-server.md](03-stt-server.md) | STT 서버(Java) 쪽 읽기·전달·오류 처리 |
| [04-license-format.md](04-license-format.md) | 필드 하나하나의 규칙, 서명 방식, 형식 진화 규칙 |
| [05-testing.md](05-testing.md) | 시험 도구와 **수용 기준 전체** |
| [06-field-operations.md](06-field-operations.md) | 현장 설치·오류 조치 |

# 02. 코드 결합 — SDK(JNI)에서 라이선스 검증하기

STT 서버는 라이선스 **파일 내용**을 문자열로 SDK에 넘깁니다. SDK는 이 모듈로 **서명을 검증**하고, 값(채널·기간 등)을 어떻게 쓸지는 SDK 정책으로 정합니다.

실행으로 검증한 참조 구현은 `integration/harness/harness_jni.cpp`의 `connectHostLicense`입니다(★ 표시 = SDK로 옮길 부분). Java 결합 테스트는 `integration/harness/java/licenseharness/HarnessTest.java`에 있습니다.

## 1. 전체 흐름

```
STT 서버 (Java)                                   STT SDK (C++/JNI)                             stt_license
────────────────────────────                     ─────────────────────────────────             ──────────────────
bytes = readAllBytes(LICENSE_FILE_PATH)
hostLicense = new String(bytes, UTF_8)
createSonaSTT(cb, configFile, hostLicense) ──▶  connect(cb, configFile, hostLicense)
                                                 ① configFile 에서 검증 수준 읽기 (signature|full)
                                                 ② jstring → UTF-16 → 표준 UTF-8 ─────────────▶ Utf16ToUtf8
                                                 ③ 검증 ───────────────────────────────────────▶ Verify(text, mode, …)
                                                 ④ 실패 → 1000+코드 반환 (연결 거부)
                                                 ⑤ 성공 → SDK 정책으로 값 사용 ◀─────────────── VerifiedLicense.GetInt(…)
```

## 2. API 요약 (`#include "stt_license/license.h"`, 네임스페이스 `stt::license`)

모든 함수는 예외를 던지지 않고 `Error`를 반환합니다(`std::bad_alloc` 제외). **실패하면 출력 인자를 채우지 않습니다.**

| 함수 | 하는 일 | 실패 코드 |
|---|---|---|
| `VerifySignature(text, &license, &detail)` | **서명만**: 크기(64 KiB), BOM 제거, 엄격 JSON, `signature` 형식(표준 Base64·64바이트), Ed25519 | `FileTooLarge`, `ParseError`, `BadSignature`, `Internal`(내장 키 손상) |
| `VerifyLicense(text, &license, &fields, &detail)` | 서명 + 표준 필드(필수·범위·`format_version`) + **기간**(시스템 시각). 유예 기간은 성공 | 위 + `InvalidField`, `UnsupportedVersion`, `NotYetValid`, `Expired` |
| `Verify(text, mode, &license, &fields, &detail)` | `SignatureOnly`면 `VerifySignature`, `Full`이면 `VerifyLicense`와 **결과가 완전히 같음** | 모드에 따름 |
| `ParseVerifyMode("signature"/"full", &mode)` / `VerifyModeName(mode)` | 설정 문자열 ↔ 모드. 그 외 문자열이면 `false` | — |
| `license.GetString/GetInt/GetBool(path, &out)`, `Has(path)` | 서명된 내용에서 값 조회. 경로는 점으로 구분(`"allowed_channels.offline_stt"`). 없거나 타입이 다르면 `false` | — |
| `license.canonical_json()` | 서명 대상 바이트(정규 JSON). SDK의 JSON 라이브러리로 직접 해석해도 됨 | — |
| `ParseStandardFields(license, &fields)` | 표준 규칙으로 필드 해석(선택 도우미) | `InvalidField`, `UnsupportedVersion` |
| `StateAt(fields, now)` | `NotYetValid` / `Valid` / `Grace` / `Expired` | — |
| `Utf16ToUtf8(ptr, len, &out)` | UTF-16 → 표준 UTF-8. 짝 없는 서로게이트면 `false` | — |
| `ErrorName(e)` / `ErrorMessage(e)` | `"LICENSE_BAD_SIGNATURE"` / 현장 조치 문구(영문) | — |

`LicenseFields`: `format_version`, `license_id`, `project_name`, `license_type`, `site_id`, `warning_notice`, `online_stt`, `offline_stt`, `issued_at`, `not_before`, `not_after`(UTC epoch 초), `grace_period_days`.

## 3. `jstring` → UTF-8: `GetStringUTFChars`를 쓰면 안 된다

`GetStringUTFChars`는 표준 UTF-8이 아니라 **변형 UTF-8(Modified UTF-8)**을 돌려줍니다.
- 보조 평면 문자(이모지 등)는 4바이트가 아니라 6바이트가 되고, NUL은 `C0 80`이 됩니다.
- 그러면 **서명한 바이트와 달라져 정상 라이선스가 검증에 실패합니다.**
- 실제 JVM에서 확인한 결과: 이모지가 든 라이선스는 `GetStringUTFChars` 경로에서 `1003`, 아래 방법으로는 `0`. ASCII와 한글만 있으면 두 방법 모두 통과해서 **평소 테스트로는 드러나지 않습니다.**

```cpp
namespace lic = stt::license;

// ★ jstring → 표준 UTF-8 (UTF-16 으로 받아 변환). 결과는 NUL 을 포함할 수 있으므로 길이와 함께 다룬다.
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

## 4. `connect` 구현 예

```cpp
// configFile("--key=value", '#' 주석)에서 검증 수준을 읽는다.
// 키가 없으면 signature(기존 SDK 동작 유지), 값이 잘못되면 false → 연결 거부(오타로 검증이 약해지지 않게).
static bool ReadVerifyMode(const std::string& config_file, lic::VerifyMode* mode) {
  *mode = lic::VerifyMode::SignatureOnly;
  std::ifstream f(config_file);
  const std::string key = "--license-verify-mode=";
  for (std::string line; std::getline(f, line);) {
    line = line.substr(0, line.find('#'));
    const size_t b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos) continue;
    line = line.substr(b, line.find_last_not_of(" \t\r") - b + 1);
    if (line.compare(0, key.size(), key) == 0) return lic::ParseVerifyMode(line.substr(key.size()), mode);
  }
  return true;
}

JNIEXPORT jint JNICALL Java_<패키지>_<클래스>_connect(JNIEnv* env, jobject self, jobject callback,
                                                     jstring configFile, jstring hostLicense) {
  try {
    std::string cfg = /* 기존 SDK 방식으로 configFile 경로 획득 */;
    lic::VerifyMode mode;
    if (!ReadVerifyMode(cfg, &mode)) return /* 설정 오류 코드 */ 2;
    if (hostLicense == nullptr) return /* 잘못된 인자 코드 */ 2;

    std::string text;
    lic::Error e = JStringToUtf8(env, hostLicense, &text);
    lic::VerifiedLicense license;
    lic::LicenseFields fields;          // Full 모드에서만 채워진다
    std::string detail;
    if (e == lic::Error::Ok) e = lic::Verify(text, mode, &license, &fields, &detail);
    if (e != lic::Error::Ok) {
      // 로그: lic::ErrorName(e), detail, lic::VerifyModeName(mode)
      return 1000 + static_cast<int>(e);                             // 연결 거부
    }
    // ---- 여기부터 SDK 정책 (§7) ----
    // ... 기존 connect 처리 계속 (세션 풀 생성 등)
    return 0;
  } catch (...) {
    return /* 내부 오류 코드 */ 9;     // C++ 예외가 JVM 으로 넘어가면 프로세스가 죽는다
  }
}
```
분리 함수를 직접 써도 됩니다: `lic::VerifySignature(text, &license)` / `lic::VerifyLicense(text, &license, &fields)`.

## 5. 검증 수준 선택

| 설정(`--license-verify-mode`) | 동작 | 용도 |
|---|---|---|
| 없음 또는 `signature` | 서명만. 만료된 라이선스도 서명이 맞으면 통과 | **기본.** 기존 SDK와의 호환 장치 — 기간은 SDK의 별도 만료 장치가 적용 |
| `full` | 서명 + 표준 필드 + 기간(미개시 `1007`, 만료 `1008`, 유예는 통과) | 코어 규칙을 그대로 쓸 때 |
| 그 외(오타 포함, 대소문자 구분) | 연결 거부 | 조용히 약한 검증으로 넘어가지 않게 |

`signature` 모드의 전제(2026-09-30 결정 검토):
- 받아 주는 것은 **새 형식(서명된 JSON)** 라이선스뿐입니다.
- SDK의 별도 만료 장치가 **새 형식 라이선스로 연결할 때도** 실제로 걸리는지 확인해야 합니다. 그 장치가 기존 `.key`에서 만료일을 읽는 구조라면, `license.GetString("validity.not_after", …)` 값을 넘겨야 합니다.

## 6. 기존 라이선스(`sonaLicense.*.key`)와 공존

기존 `.key`는 이 모듈로는 **어떤 모드로도 검증되지 않습니다**(`1003`, 실제 파일로 확인). 두 형식을 함께 받으려면 SDK가 분기합니다.

```cpp
lic::Error e = lic::Verify(text, mode, &license, &fields, &detail);
if (e == lic::Error::ParseError && legacy_enabled) {
  // JSON 이 아닌 입력만 기존 경로로 보낸다. BadSignature 등 다른 실패는 절대 기존 경로로 넘기지 않는다
  // (새 형식을 변조한 파일이 기존 경로로 우회하지 못하게).
  return ConnectWithLegacyKey(text.data(), text.size());   // 길이 기반 버퍼로 전달 (NUL 포함 가능)
}
```
- **분기 조건은 `ParseError`만** 씁니다. 새 형식 라이선스의 변조(`1004`)나 만료(`1008`)가 기존 경로로 새지 않게 하기 위해서입니다.
- 기존 `.key`는 ASCII 범위 바이트(제어문자 포함)로 되어 있어서, 서버가 `readAllBytes` → UTF-8 문자열로 넘겨도 원본이 보존됩니다(실제 파일로 확인). 단, 파일에 **NUL 바이트**가 있습니다(확인된 샘플에 1개).
  - §3의 UTF-16 경로는 NUL까지 그대로 복원합니다(하네스 테스트로 확인).
  - `GetStringUTFChars`는 NUL을 `C0 80`으로 바꿔 **기존 해독기 입력이 달라집니다.**
  - 복원한 바이트는 NUL을 포함하므로 `c_str()`이 아니라 **(포인터, 길이)**로 넘겨야 합니다.
- 기존 경로를 허용할지(`legacy_enabled`)는 설정으로 둡니다. 어느 경로로 연결했는지 로그에 남깁니다.
- 기존 해독기가 파일 경로만 받는 구조라면, 기존 설정(`--sonastt-license-license-path`)을 그대로 쓰는 편이 단순합니다.

## 7. 값 사용 (SDK 정책 예)

```cpp
std::int64_t offline;
if (license.GetInt("allowed_channels.offline_stt", &offline)) {
  // offline_stt = sonastt-offline + whisper-offline 동시 사용 합계.
  // 예) 엔진별 세션 수 = min(cfg num-sessions, offline), 동시 예약 합계 ≤ offline
}
std::string not_after;
if (license.GetString("validity.not_after", &not_after)) {
  // signature 모드에서 SDK 의 만료 장치에 넘길 값 ("YYYY-MM-DDTHH:MM:SSZ", UTC)
}
```
- 값은 **서명된 내용에서만** 나옵니다. 서명 범위 밖의 값은 존재할 수 없습니다.
- `signature` 모드에서 받은 값은 **형식·범위 검증을 거치지 않았습니다.** 정책에 쓰기 전에 SDK가 범위를 확인합니다(예: 채널 수 음수 불가 — 파서가 이미 거부, 상한은 SDK가 판단). 표준 규칙을 그대로 쓰려면 `ParseStandardFields`를 부르면 됩니다.

## 8. 미결 사항 — 라이선스 갱신 (추후 결정)

SDK는 파일 경로를 모르므로 자동 재적재가 없습니다. **지금은 갱신하려면 서버를 재시작**(=`connect` 재호출)해야 합니다. 결정 전에 분석할 항목:
1. 세션(채널) 수 반영: `connect` 때 만든 세션 풀(GPU 메모리)의 증감, 진행 중인 작업 처리
2. `connect` 재호출이 안전한지, 아니면 `updateLicense(String)` 같은 별도 native가 필요한지
3. `full` 모드도 기간을 `connect` 시점에만 검사하므로, 재시작 없이 오래 도는 서버는 만료 후에도 동작합니다. 운영 중 재판정 방식도 함께 정해야 합니다.

## 9. 오류 코드

JNI 반환값은 `1000 + Error 순번`입니다. 순번은 헤더의 `static_assert`로 고정되어 있습니다.

| 코드 | `ErrorName` | 뜻 | 방식 |
|---|---|---|---|
| 1001 | `LICENSE_FILE_NOT_FOUND` | 파일 없음·읽기 실패 | Manager |
| 1002 | `LICENSE_FILE_TOO_LARGE` | 64 KiB(문자열은 64K 문자) 초과 | 공통 |
| 1003 | `LICENSE_PARSE_ERROR` | JSON이 아님·규격 위반(기존 `.key` 포함) | 공통 |
| 1004 | `LICENSE_BAD_SIGNATURE` | 서명 누락·형식 오류·불일치(변조, 다른 키) | 공통 |
| 1005 | `LICENSE_INVALID_FIELD` | 필수 필드·범위·시각 형식 위반 | full, Manager |
| 1006 | `LICENSE_UNSUPPORTED_VERSION` | `format_version` 미지원 | full, Manager |
| 1007 | `LICENSE_NOT_YET_VALID` | 시작 전 | full, Manager |
| 1008 | `LICENSE_EXPIRED` | 유예 기간까지 종료 | full, Manager |
| 1009 | `LICENSE_CHANNEL_LIMIT` | 채널 한도 초과 | Manager |
| 1010 | `LICENSE_INTERNAL_ERROR` | 내장 공개키 손상 등 | 공통 |

## 10. 구현 규칙

- **JNI 진입점은 전부 `try { … } catch (...)`로 감쌉니다.** C++ 예외가 JVM으로 넘어가면 프로세스가 종료됩니다.
- **스레드:** `VerifySignature`, `VerifyLicense`, `Verify`는 상태가 없어 여러 스레드에서 동시에 호출해도 됩니다. `VerifiedLicense`는 불변이라 복사본을 여러 스레드에서 읽어도 됩니다.
- **자원:** 모듈은 스레드를 만들지 않고, 시그널 핸들러를 등록하지 않으며, 전역 초기화 순서에 의존하지 않습니다.
- **언어:** 공개 헤더는 C++11 이상에서 포함할 수 있습니다. 구현은 C++17로 빌드됩니다([01-build.md](01-build.md)).

---

## 부록 A. (선택) 파일 경로 + 값 정책 내장 방식 — `Manager`

라이선스 **경로**를 SDK가 직접 알고, 기간·채널 한도·60초 재적재까지 모듈에 맡기는 방식입니다. 현재 STT 서버 구조(문자열 전달)에서는 쓰지 않지만, 필요하면 선택할 수 있습니다. 참조 구현은 하네스의 `connect`/`getIdleSession`/`finishTask`입니다.

| SDK 함수 | 동작 |
|---|---|
| 연결 | `Manager::Open(path, &mgr)`. 미개시·만료·서명 오류면 실패. 엔진별 세션 수 = `min(cfg, mgr->MaxChannels(Kind::Offline))` |
| 세션 예약 | `mgr->Acquire(Kind::Offline, &session.channel)`. 한도 초과 `1009`, 만료 `1008` |
| 작업 종료·중단 | `session.channel.Release()` (또는 세션 객체 소멸, RAII) |
| 예약 후 작업 미제출 | 예약 타임아웃(권장 60초) 뒤 `Release()` |
| 상태 조회 | `InfoToJson(mgr->Snapshot())` |
| 연결 해제 | 세션 해제 후 `mgr.reset()` |

- 파일 재확인은 `Acquire`·`Snapshot`·`MaxChannels`를 호출할 때 60초 간격으로 합니다.
- 서명 오류·만료·미개시 파일로의 교체는 거부하고 기존 라이선스를 유지합니다.
- 경로는 일반 파일만 받습니다. FIFO·디렉터리는 즉시 `1001`로 거부해 멈추지 않습니다.

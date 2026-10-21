# 02. 코드 결합 — SDK(JNI)에서 라이선스 검증하기

STT 서버는 라이선스 **파일 내용**을 문자열로 SDK에 넘깁니다. SDK는 이 모듈로 라이선스를 **판정**(서명·표준 필드·기간)하고, 통과하면 판정된 값(채널 상한)을 세션 풀과 동시 예약에 적용합니다. 검사 수준은 라이브러리가 정하며, 서명만 보는 방식은 없습니다(3.0.0, D46).

실행으로 검증한 참조 구현은 `integration/harness/harness_jni.cpp`의 `connectHostLicense`입니다(★ 표시 = SDK로 옮길 부분). Java 결합 테스트는 `integration/harness/java/licenseharness/HarnessTest.java`에 있습니다.

## 1. 전체 흐름

```
STT 서버 (Java)                                   STT SDK (C++/JNI)                             stt_license
────────────────────────────                     ─────────────────────────────────             ──────────────────
bytes = readAllBytes(LICENSE_FILE_PATH)
hostLicense = new String(bytes, UTF_8)
createSonaSTT(cb, configFile, hostLicense) ──▶  connect(cb, configFile, hostLicense)
                                                 ① jstring → UTF-16 → 표준 UTF-8 ─────────────▶ Utf16ToUtf8
                                                 ② 판정(서명·필드·기간) ──────────────────────▶ VerifyLicense(text, …)
                                                 ③ 실패 → 1000+코드 반환 (연결 거부)
                                                 ④ 성공 → 판정된 값으로 SDK 초기화 ◀────────── LicenseFields
```

## 2. API 요약 (`#include "stt_license/license.h"`, 네임스페이스 `stt::license`)

모든 함수는 예외를 던지지 않고 `Error`를 반환합니다(`std::bad_alloc` 제외). **실패하면 출력 인자를 채우지 않습니다.**

| 함수 | 하는 일 | 실패 코드 |
|---|---|---|
| `VerifyLicense(text, &license, &fields, &detail)` | **문자열 방식의 유일한 판정.** 크기(64 KiB) → BOM 제거 → 엄격 JSON → `signature` 형식(표준 Base64·64바이트)·Ed25519 → 표준 필드(필수·범위·`license_ver`) → **기간**(시스템 시각) | `FileTooLarge`, `ParseError`, `BadSignature`, `InvalidField`, `UnsupportedVersion`, `NotYetValid`, `Expired`, `Internal`(내장 키 손상) |
| `license.GetString/GetInt/GetBool(path, &out)`, `Has(path)` | 판정을 통과한 라이선스에서 값 조회. 경로는 점으로 구분(`"allowed_channels.offline.sonastt"`). 없거나 타입이 다르면 `false`. 표준 필드는 `fields`로 받고, 이것은 그 밖의 값(추가형 새 필드 등)용 | — |
| `license.canonical_json()` | 서명 대상 바이트(정규 JSON) | — |
| `StateAt(fields, now)` | `NotYetValid` / `Valid` / `Expired`(유예 기간 없음). 운영 중 기간 재판정용 | — |
| `CheckModeName(fields.check_mode)` | 적용한 판정 수준 `"full"` / `"minimal"`(04-license-format.md §5). 로그용 | — |
| `LibraryVersion()` / `MaxLicenseVer()` | 이 라이브러리 버전(예 `"3.0.0"`)과 전체 규칙으로 판정하는 가장 높은 `license_ver`(예 `1`). `connect` 로그에 남겨, 현장 SDK가 어떤 형식까지 아는지 확인하는 근거로 씀 | — |
| `Utf16ToUtf8(ptr, len, &out)` | UTF-16 → 표준 UTF-8. 짝 없는 서로게이트면 `false` | — |
| `ErrorName(e)` / `ErrorMessage(e)` | `"LICENSE_BAD_SIGNATURE"` / 현장 조치 문구(영문) | — |

`LicenseFields`: `check_mode`(적용한 수준. `Minimal`이면 안정 필드만 판정했고 나머지는 비어 있을 수 있음), `license_ver`, `license_id`, `project_name`, `license_type`, `solution`, `site_id`, `warning_notice`, `total_channels`, `channels`(`std::map<std::string, std::map<std::string, int>>`, 항목 → 세부 항목 → 상한. 항목 이름은 자유, 예: `offline`·`online`), `issued_at`, `not_before`, `not_after`(UTC epoch 초).

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
JNIEXPORT jint JNICALL Java_<패키지>_<클래스>_connect(JNIEnv* env, jobject self, jobject callback,
                                                     jstring configFile, jstring hostLicense) {
  try {
    if (hostLicense == nullptr) return /* 잘못된 인자 코드 */ 2;
    // configFile 은 SDK 자체 설정용이다. 라이선스 검사 수준은 설정으로 바꿀 수 없다.

    std::string text;
    lic::Error e = JStringToUtf8(env, hostLicense, &text);
    lic::VerifiedLicense license;
    lic::LicenseFields fields;          // 성공할 때만 채워진다
    std::string detail;
    if (e == lic::Error::Ok) e = lic::VerifyLicense(text, &license, &fields, &detail);
    if (e != lic::Error::Ok) {
      // (기존 .key 공존을 켰다면 e == ParseError 일 때만 기존 경로로 — §6)
      // 로그: lic::ErrorName(e), detail, lic::LibraryVersion(), lic::MaxLicenseVer()
      return 1000 + static_cast<int>(e);                             // 연결 거부
    }
    // 로그: lic::CheckModeName(fields.check_mode), lic::LibraryVersion(), lic::MaxLicenseVer()
    //   ("minimal" 이면 이 SDK 가 라이선스 형식을 모름 = SDK 업그레이드 대상. 연결은 정상)
    // ---- 여기부터 SDK 초기화 (§7): 세션 풀 크기 = min(설정값, fields 의 세부 항목 상한) ----
    // ... 기존 connect 처리 계속 (모델 백엔드, 세션 풀 생성 등)
    return 0;
  } catch (...) {
    return /* 내부 오류 코드 */ 9;     // C++ 예외가 JVM 으로 넘어가면 프로세스가 죽는다
  }
}
```

`connect` 순서 규칙(2026-10-21 흐름 검토):
- **라이선스 판정을 맨 앞에 둡니다.** 판정이 `Ok`일 때만 모델 백엔드(onnx·ggml)를 준비하고, 세션 풀 크기는 라이선스 값으로 정합니다(§7). 판정 전에 모델·GPU 메모리를 잡지 않습니다.
- **판정 통과를 전역 상태로 먼저 남기지 않습니다.** 라이선스 값은 초기화가 모두 성공한 뒤에 반영합니다. 초기화 도중 실패하면 잡은 자원을 풀고 오류를 반환합니다. 반쯤 초기화된 상태가 남으면 다음 호출이 판정 없이 지나갈 수 있습니다.
- **검사만으로 끝나지 않습니다.** 채널 상한은 판정이 아니라 값 적용입니다. 세션 풀·동시 예약에 쓰지 않으면 상한이 집행되지 않습니다.
- **두 번째 `connect` 호출을 정합니다.** 이미 연결된 상태에서 다시 부르면 거부할지, 기존 자원을 정리하고 다시 판정할지 정합니다(§8 갱신 방식과 같은 문제). 정하기 전에는 거부합니다.
- **`callback`은 로컬 참조입니다.** 네이티브 작업 스레드에서 콜백을 부르려면 `NewGlobalRef`로 붙잡고, 그 스레드는 `AttachCurrentThread`로 JVM에 붙입니다. 라이선스와 무관하지만 같은 함수에서 생기는 문제입니다.
- **반환 코드 범위를 확인합니다.** 라이선스 오류는 `1001~1011`입니다. SDK의 기존 오류 코드가 이 범위와 겹치지 않는지 SDK 저장소에서 확인합니다. 겹치면 STT 서버가 원인을 잘못 판단합니다.

## 5. 검사 수준

- 검사 수준은 **라이브러리가 정합니다.** `VerifyLicense`는 서명·표준 필드·기간을 모두 판정하고, SDK 설정으로 낮출 수 없습니다.
- 그래서 만료된 라이선스는 `1008`, 시작 전 라이선스는 `1007`로 연결을 거부합니다. 운영 라이선스는 영구 값(`9999-12-31T23:59:59Z`)이라 만료되지 않습니다.
- 이전 판(2.x)의 설정 `--license-verify-mode`와 `signature` 모드(서명만 확인)는 3.0.0에서 삭제했습니다. 설정 파일에 그 키가 남아 있어도 판정에 영향이 없습니다(하네스 시험).
- 새 형식 라이선스에는 SDK의 별도 만료 장치를 쓰지 않습니다. 그 장치는 기존 `.key` 경로(§6)에만 남깁니다.
- 기간은 `connect` 때 판정합니다. 운영 중 재판정은 §8의 미결 사항입니다(`StateAt(fields, now)`로 다시 볼 수 있음).
- 형식을 올린 라이선스라도 `check_mode: "minimal"`이면 안정 필드와 기간만 판정해 받습니다(04-license-format.md §5). SDK가 할 일은 같습니다. 결과가 `Ok`면 연결하고, `fields.check_mode`를 로그에 남깁니다. 표시용 필드(`project_name` 등)는 비어 있을 수 있습니다.

## 6. 기존 라이선스(`sonaLicense.*.key`)와 공존

기존 `.key`는 이 모듈로는 **검증되지 않습니다**(`1003`, 실제 파일로 확인). 두 형식을 함께 받으려면 SDK가 분기합니다.

```cpp
lic::Error e = lic::VerifyLicense(text, &license, &fields, &detail);
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

## 7. 값 사용 (SDK 초기화)

```cpp
// fields 는 VerifyLicense 가 판정한 값이다(범위 검사를 다시 하지 않는다).
// total = 모든 항목·세부 항목의 동시 세션 합계 상한, channels["offline"]["sonastt"] = 그 세부 항목의 상한.
auto cap = [&](const std::string& group, const std::string& engine) {
  auto g = fields.channels.find(group);
  if (g == fields.channels.end()) return 0;              // 라이선스에 없는 항목 = 상한 0
  auto it = g->second.find(engine);
  return it == g->second.end() ? 0 : it->second;         // 라이선스에 없는 세부 항목 = 상한 0
};
const int sonastt_sessions = std::min(cfg_num_sessions, cap("offline", "sonastt"));
// 동시 예약은 세부 항목 상한과 fields.total_channels 를 모두 지킨다.
```
- 값은 **서명된 내용에서만** 나오고, `VerifyLicense`가 범위까지 판정한 뒤에만 채워집니다.
- 항목 이름은 라이선스마다 정할 수 있으므로, SDK가 어떤 항목·세부 항목 이름을 읽을지 미리 정해 둡니다.
- 표준 필드 밖의 값이 필요하면 `license.GetInt(path, &n)` 등으로 읽습니다(판정을 통과한 라이선스에서만).
- **동작 판단에는 안정 필드만 씁니다**(`fields.total_channels`, `fields.channels`, `fields.not_before`·`not_after`, `fields.license_id`). `license_type`·`license_ver` 같은 다른 필드로 동작을 나누지 않습니다(`minimal` 파일에서는 비어 있을 수 있음).

## 8. 미결 사항 — 라이선스 갱신 (추후 결정)

SDK는 파일 경로를 모르므로 자동 재적재가 없습니다. **지금은 갱신하려면 서버를 재시작**(=`connect` 재호출)해야 합니다. 결정 전에 분석할 항목:
1. 세션(채널) 수 반영: `connect` 때 만든 세션 풀(GPU 메모리)의 증감, 진행 중인 작업 처리
2. `connect` 재호출이 안전한지, 아니면 `updateLicense(String)` 같은 별도 native가 필요한지
3. 기간은 `connect` 시점에만 검사하므로, 재시작 없이 오래 도는 서버는 만료 후에도 동작합니다. 운영 중 재판정 방식(`StateAt`)도 함께 정해야 합니다.

## 9. 오류 코드

JNI 반환값은 `1000 + Error 순번`입니다. 순번은 헤더의 `static_assert`로 고정되어 있습니다.

| 코드 | `ErrorName` | 뜻 | 방식 |
|---|---|---|---|
| 1001 | `LICENSE_FILE_NOT_FOUND` | 파일 없음·읽기 실패 | Manager |
| 1002 | `LICENSE_FILE_TOO_LARGE` | 64 KiB(문자열은 64K 문자) 초과 | 공통 |
| 1003 | `LICENSE_PARSE_ERROR` | JSON이 아님·규격 위반(기존 `.key` 포함) | 공통 |
| 1004 | `LICENSE_BAD_SIGNATURE` | 서명 누락·형식 오류·불일치(변조, 다른 키) | 공통 |
| 1005 | `LICENSE_INVALID_FIELD` | 필수 필드·범위·시각 형식 위반 | 공통 |
| 1006 | `LICENSE_UNSUPPORTED_VERSION` | `license_ver`가 1이 아님(이 엔진이 모르는 버전). `license_ver`가 없으면 `1005` | 공통 |
| 1007 | `LICENSE_NOT_YET_VALID` | 시작 전 | 공통 |
| 1008 | `LICENSE_EXPIRED` | 종료 시각 지남 | 공통 |
| 1009 | `LICENSE_CHANNEL_LIMIT` | 세부 항목 상한 또는 `total` 초과 | Manager |
| 1010 | `LICENSE_INTERNAL_ERROR` | 내장 공개키 손상 등 | 공통 |
| 1011 | `LICENSE_ENGINE_NOT_LICENSED` | 라이선스에 없거나 상한 0인 항목·세부 항목 | Manager |

## 10. 구현 규칙

- **JNI 진입점은 전부 `try { … } catch (...)`로 감쌉니다.** C++ 예외가 JVM으로 넘어가면 프로세스가 종료됩니다.
- **스레드:** `VerifyLicense`는 상태가 없어 여러 스레드에서 동시에 호출해도 됩니다. `VerifiedLicense`는 불변이라 복사본을 여러 스레드에서 읽어도 됩니다.
- **자원:** 모듈은 스레드를 만들지 않고, 시그널 핸들러를 등록하지 않으며, 전역 초기화 순서에 의존하지 않습니다.
- **언어:** 공개 헤더는 C++11 이상에서 포함할 수 있습니다. 구현은 C++17로 빌드됩니다([01-build.md](01-build.md)).

---

## 부록 A. (선택) 파일 경로 + 값 정책 내장 방식 — `Manager`

라이선스 **경로**를 SDK가 직접 알고, 기간·채널 한도·60초 재적재까지 모듈에 맡기는 방식입니다. 현재 STT 서버 구조(문자열 전달)에서는 쓰지 않지만, 필요하면 선택할 수 있습니다. 참조 구현은 하네스의 `connect`/`getIdleSession`/`finishTask`입니다.

| SDK 함수 | 동작 |
|---|---|
| 연결 | `Manager::Open(path, &mgr)`. 미개시·만료·서명 오류면 실패. 엔진별 세션 수 = `min(cfg, mgr->MaxChannels("offline", "sonastt"))`(항목, 세부 항목). 전체 상한은 `mgr->TotalChannels()` |
| 세션 예약 | `mgr->Acquire("offline", "sonastt", &session.channel)`. 세부 항목 상한·`total` 초과 `1009`, 라이선스에 없는 항목·세부 항목 `1011`, 만료 `1008` |
| 작업 종료·중단 | `session.channel.Release()` (또는 세션 객체 소멸, RAII) |
| 예약 후 작업 미제출 | 예약 타임아웃(권장 60초) 뒤 `Release()` |
| 상태 조회 | `InfoToJson(mgr->Snapshot())` |
| 연결 해제 | 세션 해제 후 `mgr.reset()` |

- 파일 재확인은 `Acquire`·`Snapshot`·`MaxChannels`를 호출할 때 60초 간격으로 합니다.
- 서명 오류·만료·미개시 파일로의 교체는 거부하고 기존 라이선스를 유지합니다.
- 경로는 일반 파일만 받습니다. FIFO·디렉터리는 즉시 `1001`로 거부해 멈추지 않습니다.

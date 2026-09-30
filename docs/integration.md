# SDK·서버 적용 가이드

이 저장소의 `stt_license` 코어를 STT SDK(`libsonastt_jni_v2.so`)와 Spring Boot 서버(`sona-stt-tek.jar`)에 적용하는 절차다.
결합 방식은 `integration/harness/`(모의 SDK + Java 테스트)에서 실행으로 검증했다. 실제 SDK에는 하네스의 ★ 표시 지점만 옮긴다.

> 기준: `sona-stt-tek-server:0.39.0` 이미지에서 확인한 구조 (Ubuntu 22.04, glibc 2.35, Java 17, Spring Boot 3.5,
> SDK는 OpenSSL 1.1.1 정적 링크, JNI 클래스 `sonaspeech.v2.sonastt.api.SonaSttAPI`).

---

## 1. SDK (C++)

### 1.1 빌드에 포함

SDK의 기존 CMake에 서브프로젝트로 추가한다. 옵션, OpenSSL 선택 규칙, 문제 해결은 **`docs/sdk-cmake.md`**에 있다.
```cmake
set(STT_LICENSE_PUBLIC_KEY_FILE ${CMAKE_SOURCE_DIR}/keys/stt_license_public.key CACHE FILEPATH "")
add_subdirectory(third_party/licenseServer EXCLUDE_FROM_ALL)
target_link_libraries(sonastt_jni_v2 PRIVATE stt_license::embedded)
stt_license_harden_shared_library(sonastt_jni_v2)
```
- 코어는 C++17로 빌드되고, 공개 헤더는 C++11 이상이면 포함할 수 있다.
- libcrypto는 **SDK가 이미 쓰는 OpenSSL을 재사용**해야 한다. 한 `.so`에 두 벌이 들어가면 안 된다(`sdk-cmake.md` §5).
- 배포 빌드는 Ubuntu 22.04(glibc 2.35)에서 한다.

### 1.2 심볼 은닉 — 필수 보안 조치

현재 `libsonastt_jni_v2.so`는 정적 링크한 OpenSSL 심볼을 포함해 **24,033개의 심볼을 export**한다. 이 상태에서는 `LD_PRELOAD`로 `EVP_DigestVerify`를 바꿔치기하면 **변조된 라이선스가 통과한다**(`scripts/check.sh` 6단계에서 실측).
위의 `stt_license_harden_shared_library()`가 JNI 진입점만 남기고 나머지를 모두 숨긴다. 적용한 뒤에는 다음 명령으로 확인한다.
```bash
nm -D --defined-only libsonastt_jni_v2.so | awk '{print $3}' | grep -v '^Java_'   # 출력이 없어야 함
```

### 1.3 기존 라이선스(CFoneLicense) 대체

| 기존 | 변경 |
|---|---|
| `sona_license/SonaLicense.cc`, `CFoneLicense` | 제거. `stt::license::Manager` 사용 |
| `--sonastt-license-license-path/time-lock/solution/ip-check` (`sonastt_service.cfg`) | 제거. `--license-file=/app/license/license.lic` 한 개로 대체(없으면 환경변수 `LICENSE_FILE_PATH`) |
| `sonaLicense.SonaSTT.*.key` | `license.lic` (평문 JSON) |
| `sonasttw.cfg`의 `--license-file=license.lic` | 이름이 같으므로 혼동하지 않게 하나로 통합 |

### 1.4 결합 지점

| SDK 함수 | 추가할 동작 | 실패 시 |
|---|---|---|
| `connectSonaSTT` | `Manager::Open(path, &mgr)`. 엔진별 세션 수 = `min(cfg num-sessions, mgr->MaxChannels(Kind::Offline))` | `1000 + (int)Error` 반환, `ErrorName/ErrorMessage` 로그 |
| `getIdleSession` | 유휴 세션을 찾은 뒤 `mgr->Acquire(Kind::Offline, &session.channel)` | 기존 오류 JSON 형식에 `result: 1009`(한도), `1008`(만료) 등과 `ErrorName` |
| 작업 종료 (`onResult`/`onError` 호출 직후), `abortTask` | `session.channel.Release()` | — |
| 예약만 하고 `addTask`가 오지 않은 세션 | **예약 타임아웃**(기존 "task was not reserved" 처리부에 추가, 권장 60초) 후 `Release()` | — |
| `getSTTSessionInfo` | 응답 JSON에 `"license": InfoToJson(mgr->Snapshot())` 추가 | — |
| `disconnectSonaSTT` | 세션 해제(채널 소멸) 후 `mgr.reset()` | — |

- `offline_stt`는 **sonastt-offline과 whisper-offline의 합계**다. 두 엔진 모두 같은 `Kind::Offline`으로 Acquire한다. `online_stt`는 향후 스트리밍 엔진용으로 남겨 두고, 현재 SDK에서는 쓰지 않는다.
- 오류 코드는 `1000 + Error 순번`이다(1001 파일 없음, 1003 파싱, 1004 서명, 1005 필드, 1006 버전, 1007 미개시, 1008 만료, 1009 한도, 1010 내부). 순번은 헤더의 `static_assert`로 고정되어 있다.
- 코어는 스레드를 만들지 않고, 시그널 핸들러도 등록하지 않는다. 파일 재확인은 `Acquire`·`Snapshot`·`MaxChannels`를 호출할 때 60초 간격으로 한다.
- JNI 진입점은 모두 `try { … } catch (...) {}`로 감싸서 C++ 예외가 JVM으로 넘어가지 않게 한다(하네스의 `Guard`).

---

### 1.5 라이선스 내용을 문자열로 받는 방식 — 실제 `connect(callback, configFile, hostLicense)`

STT 서버는 라이선스 **파일 경로가 아니라 내용**을 SDK에 넘긴다.
```java
// STT 서버 (Java)
public int createSonaSTT(SonaSttListener callback, String configFile, String hostLicense) {
    return this.connect(callback, configFile, hostLicense);   // private native int connect(...)
}
// hostLicense 준비: 바이트를 UTF-8 로 디코드 (BOM 은 코어가 제거)
String hostLicense = new String(Files.readAllBytes(Path.of(System.getenv("LICENSE_FILE_PATH"))), StandardCharsets.UTF_8);
```

#### 검증 API 고르기
공통 코어는 **서명 검증**을 책임진다. 값(`allowed_channels`, `validity` 등)을 쓸지와 어떻게 판정할지는 기존 SDK와의 호환을 고려해 SDK가 정한다.

| 함수 | 검증 범위 | 언제 |
|---|---|---|
| `VerifySignature(text, &v)` | 서명만(크기·JSON·서명 형식·Ed25519) | 값 정책을 SDK가 직접 정할 때(기존 동작 유지) |
| `VerifyLicense(text, &v, &fields)` | 서명 + 표준 필드(필수·범위·`format_version`) + 기간(미개시·만료면 실패, 유예는 통과) | 코어 규칙을 그대로 쓸 때 |
| `Verify(text, VerifyMode, &v, &fields)` | 모드에 따라 위 둘 중 하나와 **완전히 같음** | 설정값으로 전환할 때(`ParseVerifyMode("signature"/"full")`) |
| `ParseStandardFields(v, &fields)` + `StateAt(fields, now)` | 서명 검증 뒤 필요한 판정만 골라서 | 일부만 적용할 때(예: 필드는 쓰고 기간은 무시) |
| `v.GetInt("allowed_channels.offline_stt", &n)` 등 | 값 꺼내기만(판정 없음) | 특정 값만 참조할 때. 값은 서명된 내용에서만 나온다 |

`true/false` 인자 대신 `VerifyMode` 열거형을 받는 이유: `Verify(text, true, …)`는 호출하는 곳에서 뜻이 읽히지 않고, 설정 파일 값과 바로 대응시킬 수도 없다.

#### SDK JNI 구현 예 (실행 검증: `integration/harness/harness_jni.cpp`의 `connectHostLicense`)
```cpp
#include "stt_license/license.h"
namespace lic = stt::license;

// jstring → 표준 UTF-8.  GetStringUTFChars 를 쓰면 안 된다(아래 주의 1).
static lic::Error JStringToUtf8(JNIEnv* env, jstring js, std::string* out) {
  const jsize n = env->GetStringLength(js);
  if (n > 64 * 1024) return lic::Error::FileTooLarge;
  std::vector<jchar> buf(n);
  if (n > 0) env->GetStringRegion(js, 0, n, buf.data());
  if (env->ExceptionCheck()) return lic::Error::Internal;
  return lic::Utf16ToUtf8(reinterpret_cast<const std::uint16_t*>(buf.data()), buf.size(), out)
             ? lic::Error::Ok : lic::Error::ParseError;
}

JNIEXPORT jint JNICALL Java_<패키지>_<클래스>_connect(JNIEnv* env, jobject self, jobject callback,
                                                     jstring configFile, jstring hostLicense) {
  try {
    if (hostLicense == nullptr) return /* 기존 SDK 의 잘못된 인자 코드 */ 2;
    lic::VerifyMode mode = lic::VerifyMode::SignatureOnly;           // 기본: 기존 SDK 동작 유지
    // configFile 의 "--license-verify-mode=signature|full" 을 읽는다. 값이 잘못되면 연결 거부(오타로 약해지지 않게).
    //   if (has_key && !lic::ParseVerifyMode(value, &mode)) return 2;
    std::string text;
    lic::Error e = JStringToUtf8(env, hostLicense, &text);
    lic::VerifiedLicense license;
    lic::LicenseFields fields;
    std::string detail;
    if (e == lic::Error::Ok) e = lic::Verify(text, mode, &license, &fields, &detail);
    if (e != lic::Error::Ok) { /* log lic::ErrorName(e), detail */ return 1000 + static_cast<int>(e); }

    // ---- 여기부터 SDK 정책 ----
    std::int64_t offline = 0;
    if (license.GetInt("allowed_channels.offline_stt", &offline)) { /* 세션 수 상한 = min(cfg, offline) */ }
    // 기존 FoneLicense 설정과 병행한다면: 라이선스 값이 없을 때 cfg 값 유지 등
    // ... 기존 connect 처리 계속
    return 0;
  } catch (...) {
    return /* 내부 오류 코드 */ 9;  // C++ 예외가 JVM 으로 넘어가면 프로세스가 죽는다
  }
}
```

#### 주의
1. **`GetStringUTFChars` 금지.** 이 함수는 표준 UTF-8이 아니라 **변형 UTF-8**을 돌려준다. 보조 평면 문자(이모지 등)와 NUL의 바이트가 달라서, **서명한 바이트와 달라져 정상 라이선스가 검증에 실패한다.** 실제 JVM에서 대조 실험으로 확인했다: 이모지가 든 라이선스는 `GetStringUTFChars` 경로에서 `1003`, UTF-16 경로에서 `0`. ASCII와 한글만 있으면 두 경로 모두 통과해서 평소 테스트로는 드러나지 않는다.
2. **라이선스 갱신 방식은 미정(추후 결정, 2026-09-30).** SDK가 파일 경로를 모르므로 `Manager`의 60초 재적재가 적용되지 않는다. 결정 전까지 갱신은 서버를 재시작해 `connect`를 다시 부르는 방법뿐이다. 분석할 항목:
   - 세션(채널) 수 반영: `connect` 때 이미 만든 세션 풀(GPU 메모리)을 늘리거나 줄일 수 있는가, 진행 중인 작업은 어떻게 하는가
   - `connect` 재호출이 SDK에서 안전한가, 아니면 `updateLicense(String)` 같은 별도 native가 필요한가
   - **`full` 모드도 기간을 `connect` 시점에만 검사한다.** 재시작 없이 오래 도는 서버는 만료일이 지나도 계속 동작하므로, 운영 중 재판정 방식도 함께 정해야 한다
3. **`signature` 모드(기본)는 호환 장치다(결정, 2026-09-30).** 값(기간 포함)은 코어가 판정하지 않고, SDK의 기존 만료 장치(time-lock)가 적용된다. 전제 조건:
   - 이 모드가 받아 주는 것은 **새 형식(서명된 JSON) 라이선스**뿐이다. 기존 난독화 `sonaLicense.*.key` 파일은 어떤 모드에서도 `1003`(파싱 오류)이다(실제 파일로 확인). 기존 `.key`까지 받으려면 SDK가 형식을 판별해 기존 `CFoneLicense` 경로로 보내야 한다
   - SDK의 기존 만료 장치가 **새 형식 라이선스로 연결할 때도** 실제로 걸리는지 확인한다. 만료일을 `.key` 파일에서 읽는 구조라면 새 형식에는 적용되지 않는다. 그 경우 `license.GetString("validity.not_after", …)`로 새 라이선스의 만료일을 SDK 정책에 넘겨야 한다
4. 오류 코드는 `1000 + Error 순번`이다: 1002 크기 초과, 1003 파싱, 1004 서명, 1005 필드, 1006 버전, 1007 미개시, 1008 만료, 1010 내부.

---

## 2. 서버 (Spring Boot, Java 17)

서버 소스는 이 저장소에 없다. 아래는 이미지에서 확인한 클래스 구조를 기준으로 한 **체크리스트**다.

1. **기동 게이트**: `connectSonaSTT` 반환값이 0이 아니면 예외를 던져 애플리케이션 컨텍스트 기동을 실패시킨다. 그러면 JVM이 0이 아닌 코드로 종료한다(원문의 Exit 1). 기존 `config/LicenseLoader`는 제거하거나 이 역할로 바꾼다.
2. **오류 매핑**: `getIdleSession` 결과의 `result`에 따라 처리한다.
   - `1009`(채널 한도): 배치는 대기열로 재시도하고, 실시간 요청은 "잠시 후 재시도" 응답을 준다(WebSocket close 1013 / HTTP 429).
   - `1007`, `1008`: `LICENSE_NOT_YET_VALID`, `LICENSE_EXPIRED`로 응답한다(WebSocket close 1008 / HTTP 503).
   - 기존 `SonaSttErrorCode`에 이 오류 이름을 추가한다.
3. **상태 노출**: 기존 health check 경로에서 `getSTTSessionInfo().license`를 읽는다.
   - `state`가 `GRACE`이면 1시간에 한 번 WARN 로그를 남긴다.
   - `EXPIRED`이면 ERROR 로그를 남긴다.
   - `last_reload_error`가 `OK`가 아니면 WARN 로그를 남긴다.
4. **세션 반환 보장**: WebSocket 연결 종료·전송 오류·서버 종료의 모든 경로에서 진행 중인 작업을 `abortTask`로 정리한다. SDK의 예약 타임아웃은 최후 방어선일 뿐이다.
5. **SIGHUP 금지**: JVM은 SIGHUP을 받으면 종료한다(Java 21에서 shutdown hook 실행 후 exit 129로 실측). 라이선스 갱신은 파일 교체 후 최대 60초 안에 자동으로 반영된다.
6. **Docker**
   ```yaml
   volumes:
     - /opt/app/license:/app/license:ro      # 파일이 아니라 디렉터리 마운트 (파일 교체 반영)
   environment:
     - LICENSE_FILE_PATH=/app/license/license.lic
   ```
   `nvidia_entrypoint.sh`가 `exec "$@"`로 java를 실행하므로 JVM이 PID 1이 된다. SIGTERM을 받으면 graceful shutdown이 정상적으로 동작한다.

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

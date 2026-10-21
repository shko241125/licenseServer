# 03. STT 서버(Java) 측 적용

서버는 라이선스 **파일을 읽어 문자열로 SDK에 넘기고, 반환 코드에 따라 기동 여부를 정합니다.** 서명 검증과 값 판정은 SDK(02-integration.md) 몫입니다.

## 1. 읽기와 전달

```java
// 파일 전체를 바이트로 읽어 UTF-8 로 해석한다. 줄 단위 읽기(readLine/lines)·trim 금지:
// 줄바꿈·공백·제어문자가 바뀌면 기존 .key 의 해독 결과가 달라지고, 새 형식도 64 KiB 판정이 어긋난다.
byte[] bytes = Files.readAllBytes(Path.of(licenseFilePath));
String hostLicense = new String(bytes, StandardCharsets.UTF_8);

int rc = sonaStt.createSonaSTT(callback, configFile, hostLicense);   // → native connect(...)
if (rc != 0) {
    throw new IllegalStateException("STT license rejected: " + describe(rc));  // 컨텍스트 기동 실패 → JVM 종료
}
```
- **파일 크기:** 서버에서 먼저 64 KiB 상한을 확인해 두면, 잘못된 파일(로그 등)을 통째로 넘기지 않습니다. SDK도 64K 문자를 넘으면 `1002`를 반환합니다.
- **인코딩:** `new String(bytes, UTF_8)`로 바꿔도 새 형식(UTF-8 JSON)과 기존 `.key`(ASCII 바이트) 모두 원본이 보존됩니다. 다른 문자셋(`Charset.defaultCharset()` 등)은 쓰지 않습니다.
- 기존 `config/LicenseLoader`가 있으면 이 역할로 바꿉니다.

## 2. 반환 코드 처리

| 코드 | 이름 | 서버 동작 |
|---|---|---|
| 0 | — | 정상 기동 |
| 1002~1005, 1010 | 크기·형식·서명·필드·내부 | 기동 실패. 로그 첫 줄에 이름과 조치(06-field-operations.md §3)를 남김 |
| 1007 / 1008 | 미개시 / 만료 | 기동 실패. 호스트 시계 확인 또는 갱신본 설치 |
| 1006 | 미지원 형식 버전 | 기동 실패. `license_ver`가 이 엔진보다 새 버전이고 `check_mode`가 없으면(또는 모르는 `check_mode` 값) 엔진 업그레이드 또는 호환 라이선스 요청. |
| 1009 / 1011 | 채널 소진 / 허용되지 않은 엔진 (세션 예약 시, Manager 방식 또는 SDK 정책) | 기동이 아니라 **작업 단위** 오류. `1009`는 잠시 뒤 재시도, `1011`은 재시도해도 같으므로 엔진 설정·라이선스 항목 확인 |
| 그 외 | SDK 자체 오류 코드 | 기존 처리 유지 |

기존 `SonaSttErrorCode`에 `LICENSE_*` 이름을 추가해 두면, 로그와 모니터링에서 코드 대신 이름으로 볼 수 있습니다. 이름과 문구는 헤더의 `ErrorName`/`ErrorMessage`와 같게 맞춥니다.

## 3. 설정 (`configFile`)

- 라이선스 검사 수준은 설정으로 고르지 않습니다. SDK가 서명·표준 필드·기간을 모두 판정합니다(코어 3.0.0, D46). 이전 판의 `--license-verify-mode`는 없어졌고, 남아 있어도 무시됩니다.
- 기존 `.key` 공존을 SDK에서 설정으로 켜고 끄게 했다면, 그 키는 이 파일에 둡니다(02-integration.md §6).

## 4. Docker

```yaml
volumes:
  - /opt/app/license:/app/license:ro        # 파일이 아니라 디렉터리 단위 마운트
environment:
  - LICENSE_FILE_PATH=/app/license/license.lic
```
- **디렉터리 단위로 마운트합니다.** 파일 단위 bind mount는 호스트에서 파일을 바꿔(편집기 저장·`mv`) inode가 달라지면 컨테이너에 반영되지 않습니다.
- `nvidia_entrypoint.sh`가 `exec "$@"`로 java를 실행하므로 JVM이 PID 1입니다. SIGTERM을 받으면 정상 종료됩니다.

## 5. 갱신과 신호

- **라이선스 갱신 방식은 미정입니다(02-integration.md §8).** 현재는 파일을 교체한 뒤 **컨테이너를 재시작**해야 반영됩니다.
- **`docker kill -s HUP`을 쓰지 않습니다.** JVM은 SIGHUP을 받으면 종료합니다(Java 21에서 exit 129로 확인).
- 기간은 `connect` 때 한 번만 판정합니다. 재시작 없이 오래 도는 서버는 만료일을 넘겨 계속 돌 수 있으므로, 운영 중 재판정(02-integration.md §8)이 정해질 때까지는 시험·개발 라이선스의 만료 전에 갱신본을 넣고 재시작하는 절차로 관리합니다.

## 6. 적용 확인 목록

- [ ] `readAllBytes` + `UTF_8`로 읽는다(줄 단위·trim 없음)
- [ ] `rc != 0`이면 기동이 실패하고, 로그에 코드 이름이 남는다
- [ ] 라이선스 파일이 없거나 읽기 실패(`IOException`)면 `createSonaSTT`를 부르지 않고 기동을 실패시킨다(`hostLicense`에 `null`·빈 문자열을 넘기지 않음)
- [ ] `createSonaSTT`가 실패했을 때 자동 재시도 반복문이 없다(같은 파일이면 결과가 바뀌지 않음. `1007`만 시각이 지나면 바뀌므로 재시작으로 다시 시도)
- [ ] `createSonaSTT`는 프로세스에서 한 번만 부른다(두 번째 호출 동작은 02-integration.md §4 순서 규칙)
- [ ] SDK의 기존 오류 코드가 `1001~1011`과 겹치지 않는다
- [ ] Docker 마운트가 디렉터리 단위다
- [ ] 변조 파일(`1004`), 기존 `.key`(공존 설정에 따라 성공 또는 `1003`), 만료 파일(`1008`)로 기동을 시험했다(05-testing.md §3)

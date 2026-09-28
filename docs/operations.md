# 라이선스 운영 절차서

## 1. 서명 키 생성 (최초 1회, 사내 발급 호스트)

```bash
licensectl keygen -out /secure/stt-license-keys
```
- `private.key`(권한 0600)는 **발급 호스트 밖으로 절대 반출하지 않는다.** 저장소 커밋, 메신저·메일 전송, CI 비밀변수 등록을 모두 금지한다.
- 오프라인 매체 2부에 백업해 봉인하고, 서로 다른 장소에 보관한다.
- `public.key`는 SDK 빌드 담당자에게 전달한다(`-DSTT_LICENSE_PUBLIC_KEY_FILE`). 공개키는 비밀이 아니다.
- 키 파일이 이미 있으면 명령이 실패한다(덮어쓰기 방지).
- 키는 **POSIX 권한을 지원하는 리눅스 파일시스템**에 만든다. WSL의 `/mnt/c`, FAT, 일부 네트워크 드라이브에서는 0600이 적용되지 않는다. 이런 곳에 만들면 `licensectl`이 `WARNING: ... readable by other users`를 출력한다.

## 2. 발급

계약 정보를 JSON으로 작성한다(예: `core/testdata/contract.example.json`). 다음 필드는 생략하면 자동으로 채워진다.
- `format_version`: 1
- `license_id`: 32자리 난수
- `validity.issued_at`: 발급 시각

```bash
licensectl issue -key /secure/stt-license-keys/private.key \
  -in contract.json -out license.lic -ledger /secure/stt-license-ledger
```
- 발급 직후 검증기 경로로 자기검증을 한다.
- 발급 이력은 `ledger/ledger.jsonl`에 한 줄씩 쌓이고, 사본이 `ledger/<날짜>_<site>_<license_id>.lic`로 저장된다.
- `license_type`은 `production`, `trial`, `poc` 중 하나만 쓸 수 있다.
- `grace_period_days`는 0~90, 채널 수는 0~100000이며, 둘 중 한 종류는 1 이상이어야 한다.
- 시각은 `YYYY-MM-DDTHH:MM:SSZ`(UTC) 형식만 받는다. KST 자정은 전날 `15:00:00Z`다.

## 3. 반입 전 확인 (필수)

```bash
licensectl verify -pub public.key license.lic                             # 지금 시각 기준
licensectl verify -pub public.key -at 2027-09-25T00:00:00Z license.lic    # 특정 시각 기준
```
종료 코드는 서명이 맞고 상태가 VALID 또는 GRACE이면 0, 그 밖에는 1이다.

## 4. 설치 (고객 폐쇄망)

1. 보안 USB나 망간 반입 결재로 `license.lic`를 전달한다.
2. 호스트의 `/opt/app/license/license.lic`에 둔다. 컨테이너에는 **디렉터리 단위**로 읽기 전용 마운트된다.
3. 컨테이너를 기동한다. 라이선스가 유효하지 않으면 서버가 기동하지 않고 종료되며, 로그 첫 줄에 오류 이름과 조치 방법이 나온다.

## 5. 갱신 (무중단)

1. 새 `license.lic`를 받아 3번 절차로 확인한다.
2. 호스트 파일을 교체한다. `cp`, `mv`, 편집기 저장 모두 가능하다(디렉터리 마운트이므로 반영됨).
3. **최대 60초 안에 자동으로 반영된다.** health나 세션 정보의 `license.not_after`, `license_id`로 확인한다.
4. 다음 경우에는 교체가 **거부되고 기존 라이선스가 유지된다.** 이때 `last_reload_error`에 사유가 기록된다.
   - 서명이 틀린 파일
   - 이미 만료된 파일
   - 아직 시작일(`not_before`)이 되지 않은 파일
5. 시작일이 미래인 갱신본을 미리 넣어 두면, 그 시각이 된 뒤 60초 안에 자동으로 적용된다.
6. **`docker kill -s HUP`을 쓰지 않는다.** JVM은 SIGHUP을 받으면 종료한다.

## 6. 오류별 조치

| 오류 (`result`) | 의미 | 조치 |
|---|---|---|
| `LICENSE_FILE_NOT_FOUND` (1001) | 파일 없음 또는 읽기 권한 없음 | 마운트 경로와 `LICENSE_FILE_PATH` 확인 |
| `LICENSE_FILE_TOO_LARGE` (1002) | 64 KiB 초과 | 원본 파일 재배치 |
| `LICENSE_PARSE_ERROR` (1003) | JSON 형식 위반(편집 흔적) | 원본 파일 재배치 |
| `LICENSE_BAD_SIGNATURE` (1004) | 내용이 수정되었거나 다른 키로 서명됨 | 원본 파일 재배치. 계속되면 발급처에 문의 |
| `LICENSE_INVALID_FIELD` (1005) | 필드 규격 위반 | 발급처 재발급 |
| `LICENSE_UNSUPPORTED_VERSION` (1006) | 엔진보다 새로운 형식 | 엔진 업그레이드 또는 호환 라이선스 요청 |
| `LICENSE_NOT_YET_VALID` (1007) | 시작일 이전 | 호스트 시계 확인 |
| `LICENSE_EXPIRED` (1008) | 유예 기간까지 지남 | 갱신본 설치 |
| `LICENSE_CHANNEL_LIMIT` (1009) | 허용 채널이 모두 사용 중 | 정상 동작. 지속되면 채널 증설 계약 |
| `LICENSE_INTERNAL_ERROR` (1010) | 내장 키 손상 등 | 엔진 재설치, 개발사 문의 |

유예 기간(GRACE)에는 서비스가 정상 동작하지만 경고 로그가 남는다. 유예가 끝나면 **새 작업만 거부**하고, 진행 중인 작업은 끝까지 처리한다.

## 7. 키 유출·분실

- **유출**: 새 키쌍을 만든다 → SDK를 새 공개키로 재빌드한다 → 전 고객 이미지를 재배포하고 라이선스를 재발급한다. 이전 키로 서명된 라이선스는 새 엔진에서 모두 거부된다.
- **분실**: 기존 엔진용 라이선스를 더 이상 발급할 수 없다. 대응은 유출과 같다. 그래서 백업 2부 보관이 필수다.

## 8. 알려진 한계 (계약으로 통제)

- 호스트 시계를 과거로 돌리면 만료 검사를 우회할 수 있다. 로그에 `checked_at`이 남으므로 실사 증거로 쓴다.
- 채널 수는 **컨테이너 인스턴스당** 적용된다. 같은 파일로 여러 인스턴스를 띄우는 것은 계약 위반이다.
- 바이너리 자체를 패치하는 공격은 기술적으로 막지 않는다.

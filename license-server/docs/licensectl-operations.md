# licensectl 발급 측 운영 절차 (CLI)

발급 서버를 쓰지 않고 `licensectl`로 직접 키를 만들고 발급할 때의 절차다. 웹 발급은 `../README.md`를 본다.
현장 설치·오류별 조치·알려진 한계는 SDK 문서 [06-field-operations.md](../../docs/sdk/06-field-operations.md)로 옮겼다.

## 1. 서명 키 생성 (최초 1회, 사내 발급 호스트)

```bash
licensectl keygen -out /secure/stt-license-keys
```
- `private.key`(권한 0600)는 **발급 호스트 밖으로 절대 반출하지 않는다.** 저장소 커밋, 메신저·메일 전송, CI 비밀변수 등록을 모두 금지한다.
- 오프라인 매체 2부에 백업해 봉인하고, 서로 다른 장소에 보관한다.
- `public.key`는 SDK 빌드 담당자에게 전달하고, 지문(`base64 -d public.key | sha256sum`)은 별도 경로로 알린다([01-build.md §6](../../docs/sdk/01-build.md)). 공개키는 비밀이 아니다.
- 키 파일이 이미 있으면 명령이 실패한다(덮어쓰기 방지).
- 형식: `public.key`는 Base64 44자 한 줄이고, `private.key`는 `stt-license-ed25519-private-v1:`로 시작한다. 두 파일을 서로 바꿔 지정하면 `licensectl`과 SDK 빌드 모두 거부한다.
- 키는 **POSIX 권한을 지원하는 리눅스 파일시스템**에 만든다. WSL의 `/mnt/c`, FAT, 일부 네트워크 드라이브에서는 0600이 적용되지 않는다. 이런 곳에 만들면 `licensectl`이 `WARNING: ... readable by other users`를 출력한다.

## 2. 발급

계약 정보를 JSON으로 작성한다(예: `core/testdata/contract.example.json`). 다음 필드는 생략하면 자동으로 채워진다.
- `license_ver`: 1
- `license_id`: 32자리 난수
- `validity.issued_at`: 발급 시각

```bash
licensectl issue -key /secure/stt-license-keys/private.key \
  -in contract.json -out license.lic -ledger /secure/stt-license-ledger
```
- 발급 직후 검증기 경로로 자기검증을 한다.
- 발급 이력은 `ledger/ledger.jsonl`에 한 줄씩 쌓이고, 사본이 `ledger/<날짜>_<site>_<license_id>.lic`로 저장된다.
- `license_type`은 `production`(운영), `trial`(시험·데모), `dev`(개발) 중 하나만 쓸 수 있다.
  - `production`은 `validity.not_after`가 반드시 `9999-12-31T23:59:59Z`(영구), `trial`·`dev`는 실제 종료일이어야 한다.
- 채널은 `allowed_channels.total`(1~100000) + 이름 자유 항목 `{"세부 항목": 상한}`이다. 각 상한은 0~100000이고 `total` 이하, 하나 이상은 1 이상이어야 한다. 유예 기간은 없다.
- 규격에 없는 키(오타, 이전 형식의 `format_version`·`grace_period_days` 등)가 있으면 `<키>: unknown key`로 거부한다(엄격 발급). 규격: [04-license-format.md](../../docs/sdk/04-license-format.md)
- 시각은 `YYYY-MM-DDTHH:MM:SSZ`(UTC) 형식만 받는다. KST 자정은 전날 `15:00:00Z`다.

## 3. 반입 전 확인 (필수)

```bash
licensectl verify -pub public.key license.lic                             # 지금 시각 기준
licensectl verify -pub public.key -at 2027-09-25T00:00:00Z license.lic    # 특정 시각 기준
```
종료 코드는 서명이 맞고 상태가 VALID이면 0, 그 밖에는 1이다.

## 4. 설치·갱신·오류 조치

SDK 문서 [06-field-operations.md](../../docs/sdk/06-field-operations.md)를 본다. 현재 SDK 연동(`connect(callback, configFile, hostLicense)`)에서는 **갱신 시 서버 재시작**이 필요하다(갱신 방식 미정).

## 5. 키 유출·분실

- **유출**: 새 키쌍을 만든다 → SDK를 새 공개키로 재빌드한다 → 전 고객 이미지를 재배포하고 라이선스를 재발급한다. 이전 키로 서명된 라이선스는 새 엔진에서 모두 거부된다.
- **분실**: 기존 엔진용 라이선스를 더 이상 발급할 수 없다. 대응은 유출과 같다. 그래서 백업 2부 보관이 필수다.

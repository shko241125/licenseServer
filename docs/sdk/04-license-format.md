# 04. 라이선스 파일 규격 (license_ver 1)

## 1. 예

최종 라이선스 파일(`license.lic`, 발급 도구가 만든 그대로):

```json
{
  "license_ver": 1,
  "license_id": "3f2a9c...(32자리 16진수)",
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
    "issued_at": "2026-09-20T01:23:45Z",
    "not_before": "2026-09-21T00:00:00Z",
    "not_after": "9999-12-31T23:59:59Z"
  },
  "warning_notice": "This software license is geographically restricted ...",
  "signature": "(표준 Base64 88자)"
}
```
`allowed_channels`는 `total`을 뺀 나머지 항목을 **자유롭게 정합니다**. 위의 `offline`·`online`은 기본 예시일 뿐입니다. 기본 형식:
```json
"allowed_channels": {
  "total": 16,
  "항목 이름": { "세부항목1": 8, "세부항목2": 4 },
  "다른 항목": { "세부항목1": 2 }
}
```
발급 요청(계약 입력)은 위에서 **발급 도구가 채우는 값을 뺀 것**입니다(`core/testdata/contract.example.json`):

| 필드 | 발급 요청 | 최종 파일 |
|---|---|---|
| `license_ver` | 생략 가능(→ `1`) | 필수 |
| `license_id` | 생략(발급 도구가 32자리 16진수 생성) | **필수** — 없으면 `full`·Manager `1005` |
| `validity.issued_at` | 생략(→ 발급 시각) | 필수 |
| `signature` | 넣으면 거부 | 필수(마지막 키) |
| 나머지 | 입력한 값 그대로 | 같음 |

실제 서명된 예: `core/testdata/golden_license.lic`, 공개키 `golden_public.key`, 서명 대상 바이트 `golden_canonical.json`.

## 2. 필드 (표준 규칙 = `full` 모드, `ParseStandardFields`, `Manager`)

| 경로 | 타입 | 규칙 |
|---|---|---|
| `license_ver` | 정수 | `1`. 가장 먼저 판정합니다. 다른 값이면 `1006`. 없으면 `1005`, 단 `format_version`이 있으면(이전 형식) `1006` |
| `license_id` | 문자열 | 1~128바이트 |
| `project_name` | 문자열 | 1~256바이트 |
| `license_type` | 문자열 | 1~64바이트. 발급 도구는 `production`/`trial`/`poc`만 발급(검증기는 값 제한 없음) |
| `solution` | 문자열 | 1~128바이트. 값은 자유(예: `sonastt-universal`) |
| `site_id` | 문자열 | 1~256바이트 |
| `warning_notice` | 문자열 | 1~4096바이트 |
| `allowed_channels.total` | 정수 | 1~100000. **모든 항목·세부 항목의 동시 세션 합계 상한** |
| `allowed_channels.<항목>` | 객체 | `total`을 뺀 모든 키. 이름은 자유(예: `offline`, `online`), 최대 64개. 값은 `세부 항목: 상한` 객체(빈 객체 허용, 최대 64쌍). 객체가 아니면 `1005` |
| `allowed_channels.<항목>.<세부 항목>` | 정수 | 0~100000이고 `total` 이하. 그 세부 항목(엔진 등)의 동시 세션 상한. 더 깊은 중첩은 `1005` |
| `validity.issued_at` | 시각 | 발급 시각(판정에 쓰지 않음) |
| `validity.not_before` / `not_after` | 시각 | `not_before < not_after`. 두 시각 모두 포함 구간 |
| `signature` | 문자열 | 표준 Base64(패딩 포함), 디코드 결과 64바이트 |

- 문자열 필드는 제어문자(U+0000~U+001F, U+007F)를 허용하지 않습니다. 길이는 UTF-8 **바이트** 기준입니다(한글 1자 = 3바이트).
- 항목·세부 항목 이름은 열린 목록입니다. 키 문법(§3, `[a-z0-9_]` 1~64자)만 맞으면 됩니다. `total`은 같은 객체의 키라 항목 이름으로 쓸 수 없습니다. 라이선스에 없는 항목·세부 항목은 상한 0(`1011`)으로 봅니다.
- 같은 세부 항목 이름이 여러 항목에 있으면 항목마다 따로 셉니다(`offline.sonastt`와 `online.sonastt`는 다른 상한).
- 세부 항목 중 하나는 1 이상이어야 합니다(항목이 하나도 없으면 `1005`).
- 세부 항목 상한의 합이 `total`보다 크거나 작아도 됩니다. 동시 세션은 세부 항목 상한과 `total`을 **모두** 지켜야 합니다.
- **시각 형식**: `YYYY-MM-DDTHH:MM:SSZ`(UTC, 정확히 20자)만 받습니다. 1970년 이상, 실제 달력 날짜만 허용하며 윤초(`:60`)는 받지 않습니다. 한국 시간 자정은 전날 `15:00:00Z`입니다.
- **영구 라이선스**: `not_after`가 `9999-12-31T23:59:59Z`이면 사실상 무기한입니다(검증기는 특별 취급하지 않고 일반 시각으로 비교).
- **상태 판정**(`StateAt`, 유예 기간 없음):
  - `now < not_before` → `NOT_YET_VALID`
  - `now ≤ not_after` → `VALID`
  - 그 밖 → `EXPIRED`
- **표에 없는 키**는 허용되고 서명에 포함되며, 표준 규칙은 무시합니다. SDK는 `VerifiedLicense::Get*`로 읽을 수 있습니다(§5 호환 규칙).

### 발급 정책 (`licensectl issue`·발급 서버, 검증기는 보지 않음)

| 유형 | `not_after` |
|---|---|
| `production` | 반드시 `9999-12-31T23:59:59Z`(영구) |
| `trial`, `poc` | 실제 종료일. 영구 값은 거부(발급 서버는 9999년 이후 종료도 거부) |

- 이전 형식 키(`format_version`, `allowed_channels.online_stt`/`offline_stt`, `validity.grace_period_days`)가 섞인 계약은 거부합니다.
- `license_ver`, `license_id`, `validity.issued_at`은 생략하면 자동으로 채웁니다.

## 3. JSON 문법 (모든 모드 공통 — 위반 시 `1003`)

RFC 8259의 **엄격한 부분집합**만 받습니다.
- 크기 64 KiB 이하. 파일 앞 UTF-8 BOM은 제거한 뒤 판정합니다.
- 최상위는 객체여야 합니다.
- 키는 `[a-z0-9_]` 1~64자이고, 같은 객체 안에서 중복되면 안 됩니다.
- 값은 문자열, 불리언, 객체, **0 이상 2^53−1 이하의 정수**만 됩니다. `null`, 배열, 소수·지수, 음수는 받지 않습니다.
- 중첩 깊이는 16 이하입니다.
- 문자열은 올바른 UTF-8이어야 하고, `\uXXXX` 서로게이트는 짝이 맞아야 합니다.
- 공백·줄바꿈·키 순서는 자유입니다. 서명은 정규화된 바이트에 걸리므로 들여쓰기가 달라도 검증됩니다.

## 4. 서명

1. `signature` 키를 뺀 객체를 **RFC 8785(JCS)**로 정규화합니다. 키는 UTF-16 코드 단위 순으로 정렬하고, 공백은 두지 않으며, 문자열은 최소 이스케이프로 씁니다.
   - 위 문법 제한 덕분에, 이 결과는 Python `json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)`와 바이트까지 같습니다(`scripts/check.sh` 2단계에서 교차 검증).
2. 정규화 바이트에 **Ed25519**(RFC 8032, 순수 Ed25519) 서명 → 64바이트 → 표준 Base64.
3. 검증은 같은 정규화를 거친 뒤 내장 공개키로 합니다. JDK 표준 `Signature.getInstance("Ed25519")`로도 검증됩니다(`core/tests/crosscheck/GoldenCheck.java`).

키 파일 형식:

| 파일 | 형식 |
|---|---|
| `public.key` | 32바이트 공개키의 표준 Base64 44자, 한 줄 |
| `private.key` | `stt-license-ed25519-private-v1:` + 32바이트 seed의 Base64. **SDK 패키지에 절대 포함하지 않음** |

접두어가 있어서, 비밀키를 공개키 자리에 지정하면 빌드(CMake 구성)와 `licensectl`이 모두 거부합니다.

### 다른 언어·도구로 만들거나 읽을 때 (2026-10-12 실측)

`licensectl`이 아닌 도구로 서명하거나, SDK 밖에서 라이선스 값을 다룰 때 지킬 것:

| 항목 | 규칙 | 어기면 |
|---|---|---|
| `license_id` | **최종 파일에는 반드시 있어야 함**(1~128바이트). 계약 입력에서는 생략하면 발급 도구가 채움 | `full`·Manager에서 `1005`(`license_id: must be a string`). `signature` 모드는 통과하므로 놓치기 쉬움 |
| 정수 | `1`처럼 정수로 씀. Python의 `1.0` 같은 실수 표기 금지 | `1003`(`only integers are supported`) |
| 서명 대상 바이트 | 정규화 결과를 **UTF-8 그대로**(Python은 `ensure_ascii=False`). `\uXXXX`로 이스케이프한 바이트에 서명하면 안 됨 | 한글 등 비ASCII가 있으면 `1004` |
| 영구 `not_after` | **UTC epoch 초(64비트)로 비교**(9999-12-31T23:59:59Z = 253402300799). 현지 시각으로 바꾸지 않음 | KST(+9)로 바꾸면 10000년이 됨: Python `astimezone()`은 `OverflowError`. 32비트 `time_t`는 범위를 넘음 |

확인한 조합: 코어 발급 → JDK·Node Ed25519 검증 통과, Node·Python 정규화 바이트 일치. Python 정규화 + OpenSSL 3 서명 → 코어 검증 통과. Java `Instant`·JS `Date`·PostgreSQL `timestamptz`는 영구 값을 그대로 다룸(Java는 KST 변환 시 `+10000-01-01`로 표시되나 오류는 없음).

## 5. 호환 규칙

- **필드 추가**(선택 필드):
  - `license_ver`를 올리지 않고 추가할 수 있습니다. 기존 SDK는 표준 규칙에서 이 필드를 무시합니다.
  - 새 SDK는 `Has`/`Get*`로 읽되, **값이 없을 때의 기본 동작**을 반드시 정해 둡니다.
- **`license_ver` 올림**: 기존 필드의 의미·타입·필수 여부가 바뀔 때만 올립니다. 기존 SDK는 `full` 모드에서 `1006`으로 거부합니다.
  - 단, `signature` 모드는 버전을 보지 않습니다. 버전별 분기가 필요하면 SDK가 `GetInt("license_ver")`로 확인합니다.
- **이전 형식(`format_version` 1, 코어 1.x)**: 코어 2.0.0부터 `full`·`Manager`에서 `1006`으로 거부합니다. `license_ver` 1로 재발급합니다. 서명 자체는 유효하므로 `signature` 모드에서는 통과합니다.
- **정규화 규칙과 골든 파일**(`core/testdata/golden_*`)은 바꾸지 않습니다. 바꾸면 이미 배포된 라이선스가 모두 깨집니다.
- **새 형식 라이선스를 이전 SDK(코어 1.x)에 넣으면**(실측): `full`·Manager는 `1005`(`format_version` 없음)로 거부합니다. 그런데 **기본 `signature` 모드는 통과**하고, 이전 값 경로 `allowed_channels.offline_stt`는 없습니다(`GetInt` = false). 이때 채널 한도는 그 SDK가 "값이 없을 때" 어떻게 하는지에 달려 있습니다. 따라서 **SDK를 2.0.0 기준으로 먼저 배포한 뒤** 새 형식 라이선스를 내보냅니다.
- 기존 `sonaLicense.*.key`는 이 규격이 아닙니다(항상 `1003`).

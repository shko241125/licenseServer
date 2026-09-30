# 04. 라이선스 파일 규격 (format_version 1)

## 1. 예

```json
{
  "format_version": 1,
  "license_id": "3f2a9c...(32자리 16진수)",
  "project_name": "STT Platform Modernization",
  "license_type": "production",
  "site_id": "SEOUL-MAIN-DC",
  "allowed_channels": { "online_stt": 16, "offline_stt": 8 },
  "validity": {
    "issued_at": "2026-09-20T01:23:45Z",
    "not_before": "2026-09-21T00:00:00Z",
    "not_after": "2027-09-20T23:59:59Z",
    "grace_period_days": 14
  },
  "warning_notice": "This software license is geographically restricted ...",
  "signature": "(표준 Base64 88자)"
}
```
실제 서명된 예: `core/testdata/golden_license.lic`, 공개키 `golden_public.key`, 서명 대상 바이트 `golden_canonical.json`.

## 2. 필드 (표준 규칙 = `full` 모드, `ParseStandardFields`, `Manager`)

| 경로 | 타입 | 규칙 |
|---|---|---|
| `format_version` | 정수 | `1`. 다른 값이면 `1006`(다른 필드 오류보다 먼저 판정) |
| `license_id` | 문자열 | 1~128바이트 |
| `project_name` | 문자열 | 1~256바이트 |
| `license_type` | 문자열 | 1~64바이트. 발급 도구는 `production`/`trial`/`poc`만 발급(검증기는 값 제한 없음) |
| `site_id` | 문자열 | 1~256바이트 |
| `warning_notice` | 문자열 | 1~4096바이트 |
| `allowed_channels.online_stt` | 정수 | 0~100000. 향후 스트리밍 엔진용(예약) |
| `allowed_channels.offline_stt` | 정수 | 0~100000. sonastt-offline + whisper-offline **동시 사용 합계** |
| `validity.issued_at` | 시각 | 발급 시각(판정에 쓰지 않음) |
| `validity.not_before` / `not_after` | 시각 | `not_before < not_after`. 두 시각 모두 포함 구간 |
| `validity.grace_period_days` | 정수 | 0~90. `not_after` 이후 이 일수 동안 `GRACE`(동작, 경고) |
| `signature` | 문자열 | 표준 Base64(패딩 포함), 디코드 결과 64바이트 |

- 문자열 필드는 제어문자(U+0000~U+001F, U+007F)를 허용하지 않습니다.
- `online_stt`와 `offline_stt` 중 하나는 1 이상이어야 합니다.
- **시각 형식**: `YYYY-MM-DDTHH:MM:SSZ`(UTC, 정확히 20자)만 받습니다. 1970년 이상, 실제 달력 날짜만 허용하며 윤초(`:60`)는 받지 않습니다. 한국 시간 자정은 전날 `15:00:00Z`입니다.
- **상태 판정**(`StateAt`):
  - `now < not_before` → `NOT_YET_VALID`
  - `now ≤ not_after` → `VALID`
  - `now ≤ not_after + grace × 86400` → `GRACE`
  - 그 밖 → `EXPIRED`
- **표에 없는 키**는 허용되고 서명에 포함되며, 표준 규칙은 무시합니다. SDK는 `VerifiedLicense::Get*`로 읽을 수 있습니다(§5 호환 규칙).

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

## 5. 호환 규칙

- **필드 추가**(선택 필드):
  - `format_version`을 올리지 않고 추가할 수 있습니다. 기존 SDK는 표준 규칙에서 이 필드를 무시합니다.
  - 새 SDK는 `Has`/`Get*`로 읽되, **값이 없을 때의 기본 동작**을 반드시 정해 둡니다.
- **`format_version` 올림**: 기존 필드의 의미·타입·필수 여부가 바뀔 때만 올립니다. 기존 SDK는 `full` 모드에서 `1006`으로 거부합니다.
  - 단, `signature` 모드는 버전을 보지 않습니다. 버전별 분기가 필요하면 SDK가 `GetInt("format_version")`로 확인합니다.
- **정규화 규칙과 골든 파일**(`core/testdata/golden_*`)은 바꾸지 않습니다. 바꾸면 이미 배포된 라이선스가 모두 깨집니다.
- 기존 `sonaLicense.*.key`는 이 규격이 아닙니다(항상 `1003`).

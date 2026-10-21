# 라이선스 전방 호환: 결정과 구현 계획 (코어 3.0.0)

- **상태:** 결정 완료(2026-10-21, D46). 구현 계획 확정. **1단계(`signature` 모드 삭제) 구현 완료**(2026-10-21, [remove-signature-mode.md](remove-signature-mode.md)), 2단계(`check_mode` 판정)는 구현 전. 처음 검토는 2026-10-19.
- 버전: 공개 헤더 API를 삭제하므로 **3.0.0**입니다(처음 적은 2.1.0은 버전 규칙과 어긋난 오류, §10 R11).
- **대상:** 이미 배포된 SDK(빌드할 때 고정된 정적 라이브러리)가 **나중에 형식이 바뀌어 발행된 라이선스**를 받을 수 있는가(전방 호환). 반대 방향(새 SDK가 이전 라이선스를 받음)은 §6.4.
- **목표(사용자):** 운영 중 라이선스를 다시 발행할 때 SDK를 다시 빌드하지 않는다.
- 근거: 코어 2.0.2 코드와 실측(§4.2). 문서 안의 오류 코드는 `1000 + Error 순번`입니다.

## 1. 결정 (2026-10-21, 사용자)

| # | 결정 | 이 문서에서 |
|---|---|---|
| 1 | **안 나**: 발급 서버를 추가형으로 진화(전략 1) + 서명된 `check_mode`(전략 2) + 검사 수준은 라이선스가 정함(설정 폐지) | §6, §7 |
| 2 | 안정 필드 확정: `license_id`, `allowed_channels`(`total`과 항목·세부 항목 구조, 범위), `validity.not_before`·`not_after`(시각 형식과 순서), `check_mode` 자체. **`license_type`은 넣지 않음** | §7.2 |
| 3 | 이름: 키 `check_mode`, 값 `full`·`minimal` | §7.1 |
| 4 | 지금은 SDK 첫 적용 착수 전 → 코어 3.0.0을 **첫 적용 전에** 전달. 공개 헤더를 바꿔도 깨지는 SDK가 없음 | §9 |

- 안정 필드가 아닌 필드(`license_ver`, `project_name`, `license_type`, `solution`, `site_id`, `warning_notice`, `validity.issued_at`)는 `minimal`일 때 **있으면 표시만** 하고, 없거나 형식이 달라도 거부하지 않습니다.
- `license_type`을 넣지 않았으므로 SDK는 유형으로 동작을 나누지 않습니다(표시 전용, D07과 같음). 시험·개발 라이선스의 만료는 유형이 아니라 `validity`로 모든 라이선스에 똑같이 집행됩니다.

## 2. 결론

1. **지금 그대로 버전(`license_ver`)을 올리면 이전 SDK와 호환되지 않습니다.**
   - `full` 모드와 Manager는 항상 `1006`으로 거부합니다.
   - `signature` 모드는 버전을 보지 않습니다. 그래서 비호환 형식도 통과하고, SDK가 v1 의미로 **잘못 해석합니다.**
2. **추가형 변경은 지금도 호환됩니다.** 버전을 유지하고 선택 필드만 더하는 변경입니다. 발급 서버를 추가형으로 진화시키면 추가 발행분도 이전 SDK의 `full`로 쓸 수 있습니다(§6).
3. 추가형으로 안 되는 변경에 대비해 서명된 `check_mode`를 넣고 **검사 수준을 라이선스가 정하게** 합니다(§7). SDK는 추가 검사 없이 라이브러리 결과로 사용 여부를 정합니다.
4. 이 해석 규칙은 **SDK에 처음 들어가는 코어에 있어야만** 효과가 있습니다. 코어 3.0.0으로 넣습니다.
5. 재빌드 없이 바꿀 수 있는 범위는 **안정 필드**(§7.2)를 지키는 변경뿐입니다. 안정 필드의 경로·타입·의미가 바뀌면 어떤 방법으로도 SDK를 다시 빌드해야 합니다. 새 제한 필드는 이전 SDK가 집행하지 못합니다.

## 3. 전제: 정적 라이브러리에서 고정되는 것

- `stt_license`·`stt_license_key`는 정적 라이브러리입니다(`CMakeLists.txt`). 판정 규칙과 공개키가 SDK의 `.so` 안에 빌드할 때 고정됩니다.
- **SDK가 값을 읽는 코드도 같이 고정됩니다.** 예: `fields.channels["offline"]["sonastt"]`의 경로, 타입, 의미.
- 현장의 검증 규칙은 그 SDK를 빌드할 때의 코어 버전입니다. 사이트마다 다를 수 있습니다.
- **공통 불변**(모든 버전, 모든 검사 수준): 서명 키, Ed25519, RFC 8785 정규화, 64 KiB, JSON 문법(`docs/sdk/04-license-format.md` §3). 문법은 새 필드 설계도 제한합니다(코드 `core/src/json.cpp`).
  - 키 이름 `[a-z0-9_]{1,64}`, 객체 중첩은 최상위를 포함해 16단계까지
  - 배열·실수·`null`·음수·`2^53-1`을 넘는 정수는 `1003`
  - 그래서 목록은 객체(이름 → 값)로, 음수·소수는 문자열로 설계해야 합니다.

## 4. 현재 동작 (코어 2.0.2)

### 4.1 판정 경로
- **`full` 계열:** `full` 모드, `VerifyLicense`, `ParseStandardFields`, Manager(`Open`·다시 읽기), `licensectl verify`(발급 서버 검증 화면)는 모두 같은 함수 `ExtractFields`를 거칩니다.
  - 순서: `VerifyTree`(문법·서명) → `ExtractFields`(표준 필드) → 기간
  - `ExtractFields`는 버전을 먼저 봅니다. 1이 아니면 `1006`입니다. 그다음 v1 필수 필드를 꺼냅니다.
  - 표에 없는 최상위·`validity` 키는 무시합니다. `allowed_channels`의 `total`이 아닌 키는 모두 항목 객체로 해석합니다.
- **`signature` 모드(`VerifySignature`):** `VerifyTree`까지만 갑니다. 버전과 필드는 보지 않습니다.
- **발급(`SignContract`):** 허용 키 목록(엄격 발급, D41)을 본 뒤 같은 `ExtractFields`로 자기 검증합니다. 그래서 발급기는 자기 코어가 아는 형식만 발급할 수 있습니다.

### 4.2 실측: 형식을 바꾼 라이선스를 코어 2.0.2 `full`에 넣은 결과
임시 키로 직접 서명했고, 판정 시각은 2026-10-19입니다.

| # | 형식 변경 | 결과 |
|---|---|---|
| 0 | v1 그대로 | 통과 |
| 1 | 최상위에 새 키 | **통과**(무시) |
| 2 | `validity`에 새 키 | **통과**(무시) |
| 3 | `allowed_channels`에 정수 키 | `1005` |
| 4 | `allowed_channels`에 새 항목 객체 | 통과 |
| 5 | `license_ver: 2`(나머지 v1) | **`1006`** |
| 6 | `license_ver: 2` + 새 호환 표시 키 | **`1006`**(이전 검증기는 그 키를 모름) |
| 7 | 기존 필드 타입 변경 | `1005` |
| 8 | 기존 필수 필드 삭제 | `1005` |
| 9~12 | 배열, 실수, 대문자 키 | `1003`(`signature` 모드도 거부) |
| 11 | 최상위 새 키에 중첩 객체 | 통과 |

### 4.3 이전 SDK에서 모드별 결과
| 형식 변경 | `signature` 모드 | `full` 모드 / Manager |
|---|---|---|
| 선택 키 추가(버전 유지) | 통과 | 통과 |
| 제한 키 추가(버전 유지) | 통과, **제한이 집행되지 않음** | 통과, **제한이 집행되지 않음** |
| `license_ver: 2`, 기존 필드 의미 같음 | 통과 | `1006` |
| `license_ver: 2`, 기존 필드 **의미만** 바뀜 | **통과하고 잘못 해석**(오류 없음) | `1006` |
| 기존 필드 타입·이름 변경, 삭제 | 통과. SDK의 `Get*`가 `false`를 반환하고 그 뒤는 SDK 정책대로 | `1005` |
| 문법 위반 | `1003`. 기존 `.key` 분기를 켠 SDK는 기존 해독기로 넘김(기존 해독기도 거부) | `1003` |
| 64 KiB 초과 / 서명·키 변경 | `1002` / `1004` | `1002` / `1004` |

### 4.4 운영 중 결과
- **JNI `connect`:** 판정에 실패하면 SDK 초기화가 실패하고 STT 서버가 뜨지 않습니다.
- **Manager:** 다시 읽기에 실패해도 **기존 라이선스를 유지**합니다(`last_reload_error`에 코드가 남음). 그러나 기존 라이선스가 만료되면 멈추고, 다음 재시작 때는 기동하지 못합니다. 갱신하려고 바꾼 파일이면 장애가 **만료일에 늦게** 드러납니다.

## 5. 검토 중 발견한 문제

| 문제 | 내용 | 해결 |
|---|---|---|
| `signature` 모드의 조용한 오해석 | 버전을 보지 않으므로 비호환 형식도 통과합니다. 운영 기본값이 `signature`라 영향 범위가 가장 넓습니다 | 연결 허용 판단을 `check_mode` 판정으로 통일(§7.3) |
| 설정으로 검사를 끌 수 있음 | 검증 모드는 고객 현장의 SDK 설정 파일(`--license-verify-mode`)에서 읽고, 없으면 `signature`입니다(D15). D43의 "`trial`·`dev` 서버는 `full`"을 설정 한 줄로 끌 수 있습니다. 끄면 라이브러리는 만료를 보지 않습니다 | 검사 수준을 서명된 라이선스가 정하고, 설정을 없앰(§7.3) |

## 6. 전략 1: 추가형 진화 (우선 적용)

### 6.1 규칙
버전을 올리지 않고 선택 필드만 더하면 이전 SDK의 `full`이 그대로 통과합니다. SDK를 다시 빌드할 필요가 없습니다. 새 필드를 추가하는 모든 작업은 아래 규칙을 지킵니다.

| # | 규칙 | 어기면 이전 SDK의 `full` 결과 |
|---|---|---|
| A1 | 새 값은 최상위, `validity` 안, 또는 새 최상위 객체에 둡니다 | — |
| A2 | `allowed_channels`에는 항목 객체만 둡니다. 새 제한(예: 항목별 합계 상한)도 이 안에 정수로 넣지 않습니다 | `1005` |
| A3 | v1 필수 필드(최상위 9개, `allowed_channels`·`validity` 포함, 그리고 `validity` 안 3개)는 계속 넣고, 타입·범위·의미를 바꾸지 않습니다 | `1005`, 또는 오류 없이 잘못 해석 |
| A4 | `license_ver`는 1로 유지합니다 | `1006` |
| A5 | 공통 불변(§3)을 지킵니다. 새 필드에 배열·실수·`null`·음수를 쓰지 않습니다 | `1003` / `1002` / `1004` |
| A6 | 새 입력칸을 비우면 키를 아예 넣지 않습니다. 그래서 칸을 다 비우면 지금 v1과 같은 모양이 나옵니다 | — |
| A7 | 새 필드를 쓸 때는 항상 값을 명시합니다. 버전마다 다른 기본값을 두지 않습니다 | — |
| A8 | 새 **제한** 필드에는 "SDK 코어 x.y 이상에서만 적용"이라고 화면에 표시합니다 | 조용히 집행되지 않음 |
| A9 | 새 필드 이름으로 `check_mode`와 v1 필드 이름을 쓰지 않습니다 | 의미 충돌 |
| A10 | 3.0.0 이후 모든 코어의 `full`은 **모르는 최상위·`validity` 키를 계속 무시**합니다. 추가형 진화가 기대는 동작이므로 코어 시험으로 고정합니다(§9.4) | 추가형 발급분이 `1005` |

새 SDK는 새 필드가 **있으면 집행**하고 없으면 v1처럼 동작합니다. 필드의 존재 자체가 서명되므로 이것으로 충분합니다. 필드가 없는 파일은 그 제한 없이 정당하게 발급된 것입니다.

### 6.2 발급 쪽 변경 목록 (새 필드마다)
새 필드 하나를 추가할 때 고칠 곳입니다. 2026-10-21 코드 대조로 빠진 곳을 채웠습니다.

| 계층 | 고칠 곳 |
|---|---|
| 코어 | `SignContract`의 허용 키(`kTopKeys`·`kValidityKeys`)와 필드 규칙, 출력 순서 `KeyRank()`. 새 코어 `full`의 새 필드 판정(있을 때만). 발급 코어와 SDK 코어의 버전은 달라도 됩니다 |
| `licensectl` | `verify` 요약·`-json`에 새 필드, `issue -ledger` 대장 행(필요하면) |
| 발급 서버: 계약 | `Contract`(레코드 필드, `toJson`은 값이 있을 때만 키를 넣음), `ContractValidator`, `ValidationParityTest`·`ContractValidatorTest` |
| 발급 서버: 화면 | `IssueForm`(갱신 발급 `copyOf` 포함), `licenses/form.html`, `preview.html`, `detail.html`, `verify.html`(검증 결과 키 목록) |
| 발급 서버: 저장 | `LicenseRecord`, `LicenseRepository`(`insertPending` SQL·행 매핑), `IssueService`의 감사 기록 `detail` |
| DB | 새 마이그레이션(V7 이후): NULL 허용 열 추가, `CREATE OR REPLACE FUNCTION license_guard()`로 불변 비교 목록에 새 열 추가. 적용된 V1~V6은 고치지 않습니다 |

**증명:** 새 발급분을 **SDK 최저 코어**의 `licensectl verify`로 검증하는 시험을 둡니다(C4).
- SDK 최저 코어 = 현장 SDK에 들어간 가장 오래된 코어입니다. 첫 적용이 3.0.0이므로 기준은 **3.0.0**입니다. 2.0.2는 SDK에 들어가지 않으므로 기준이 아닙니다.
- `licensectl verify`는 Manager·`VerifyLicense`와 같은 `ExtractFields`를 거치므로 라이브러리 판정의 증거가 됩니다.
- 새 코어의 자기 검증(`SignContract`, 발급 서버의 발급 직후 검증 `IssueService`)만으로는 이전 SDK가 받는다는 증명이 되지 않습니다. 둘 다 새 코어로 검사하기 때문입니다.
- 시험 단계는 첫 추가 필드를 넣는 작업에서 `check.sh`에 만듭니다. 기준 코어는 태그(`core-v3.0.0`)에서 빌드합니다.

### 6.3 비추가형 변경일 때 이전 형식(v1)으로 발급하기
원리상 가능합니다. 이전 SDK는 파일 내용과 공개키만 봅니다. 그러나 **지금 코드로 되는 것은 아닙니다.** 코어 허용 키, `Contract.LICENSE_VER`, 폼, 검증, 장부, DB `CHECK (license_ver = 1)`이 모두 v1에 고정되어 있습니다. 버전을 올리는 작업을 할 때 다음을 요구사항으로 넣어야 합니다.
- 같은 서명 키를 씁니다.
- 형식별 허용 키와 판정을 둡니다. v1 판정 코드는 고치지 않고 그대로 둡니다.
- SDK 최저 코어로 검증하는 시험을 둡니다.
- 발급 서버는 사이트별 지원 형식을 기록하고, 폼에서 형식을 고르게 합니다. 폼, 검증, DB 제약도 형식별로 둡니다.

이 비용은 D37(이전 형식 공존)에서 한 번 들였다가 D42에서 걷어낸 것과 같은 종류입니다. `minimal`(§7)이 이 비용을 대신하는 비상구입니다.

### 6.4 새 SDK가 이전 라이선스를 받는 방향
새 코어는 `license_ver` 1과 `check_mode`가 없는 파일을 **영구히** 받아야 합니다. 운영 라이선스는 영구(9999년)라서 현장에 계속 남기 때문입니다. D42("이전 형식 지원 제거")는 SDK 적용 전 형식에 대한 결정이며, 적용 이후의 v1에는 해당하지 않습니다.

## 7. 전략 2: 체크 옵션 `check_mode` (비상구)

### 7.1 값
| 값 | 라이브러리 판정 | 쓰는 때 |
|---|---|---|
| 없음 또는 `"full"` | 현행 `full`: 버전 1, v1 전체 필드, 기간 | 처음 발급분. 추가형 발급분. **지금 발급분 전부(키 없음)** |
| `"minimal"` | 서명 + 안정 필드 + 기간. 버전과 나머지 필드는 판정하지 않음 | 추가형으로 안 되는 변경을 한 추가 발행분(안정 필드 유지가 발급 조건) |
| 그 밖의 문자열 | `1006` | 미래의 다른 수준에 대비해 실패 폐쇄 |
| 문자열이 아님 | `1005` | — |

- 값은 대소문자를 구분합니다(`"Minimal"`은 `1006`).
- 추가 발행분을 순수 `signature`로 검사하지 않는 이유가 있습니다. 순수 `signature`면 기간과 채널을 라이브러리가 보지 않아, 만료와 동시 세션 제한을 SDK가 따로 구현해야 하기 때문입니다(D17의 별도 만료 장치 문제가 그대로 남음).
- `minimal`의 집행 강도는 `full`과 같습니다. 거부 사유가 서명·채널·기간이라는 점도 같습니다.
- `minimal`은 **그 형식을 모르는 검증기가 해야 할 최소 판정**입니다. 판정 함수는 3.0.0 이후 고치지 않습니다. 이후 코어가 그 `license_ver`를 안다면 그 형식의 규칙을 더 볼 수 있지만, 그 규칙은 그 코어 이후에만 적용됩니다.

### 7.2 안정 필드 (확정, 영구 고정)
규칙은 현행 v1 판정 코드(`core/src/license.cpp` `Extractor`, `ParseUtcTime`)와 같습니다.

| 경로 | 규칙 | 쓰임 |
|---|---|---|
| `check_mode` | §7.1 | 검사 수준 |
| `license_id` | 문자열 1~128바이트, 제어문자(U+0000~U+001F, U+007F) 없음 | 장부 조회, 현장 문의 |
| `allowed_channels` | 객체 | 채널 집행 |
| `allowed_channels.total` | 정수 1~100000 | 전체 동시 세션 상한 |
| `allowed_channels.<항목>` | `total`이 아닌 키는 모두 항목. 객체, 64개 이하 | 채널 집행 |
| `allowed_channels.<항목>.<세부 항목>` | 정수 0~`total`, 항목마다 64개 이하. 전체에서 1 이상인 값이 하나 이상 | 세부 항목 상한 |
| `validity` | 객체 | 기간 집행 |
| `validity.not_before`, `validity.not_after` | 정확히 `YYYY-MM-DDTHH:MM:SSZ`(20자, UTC), 1970년 이상, 실제 있는 날짜, 초 0~59. `not_before < not_after`. 유효 = `not_before ≤ 지금 ≤ not_after`(종료 시각 포함) | 기간 |

**SDK가 동작 판단에 쓰는 필드가 곧 안정 필드입니다.** 그 밖의 필드로 동작을 나누는 SDK 코드는 `minimal` 파일에서 깨집니다.

### 7.3 우선순위와 API
- 검사 수준은 **라이선스가 정합니다.** SDK 설정 `--license-verify-mode`를 없앱니다(D15·D17 대체).
- 공개 헤더에서 `VerifyMode`, `Verify(text, mode, …)`, `ParseVerifyMode`, `VerifyModeName`, `VerifySignature`, `ParseStandardFields`를 **삭제했습니다**(1단계 완료, [remove-signature-mode.md](remove-signature-mode.md)).
  - 처음 계획은 `mode` 인자를 받되 무시하는 것이었습니다(§11). 아무 일도 하지 않는 인자는 읽는 사람을 속이고, 설정을 읽는 옛 예제 코드가 조용히 남습니다. 지우면 컴파일 오류로 드러납니다.
  - SDK 적용 전이므로 깨지는 SDK가 없습니다(결정 4).
- 연결 허용 판단은 `VerifyLicense`(또는 Manager)로만 합니다. `VerifyLicense`·내부 `ParseStandardFields`·Manager·`licensectl verify`는 모두 `ExtractFields`를 거치므로 `check_mode`가 한 곳에서 적용됩니다.
- `VerifySignature`도 삭제했습니다. 서명만 보고 받아 주는 함수가 남으면 모드를 지운 효과가 없기 때문입니다(2026-10-21 사용자 요청). 서명 단계는 내부(`VerifySignatureWith`, `detail.h`)에만 남습니다. 기존 `.key` 분기(`ParseError`일 때만)는 그대로입니다(D16).

### 7.4 판정 순서
실패하면 그 단계에서 멈춥니다. `full`·`minimal` 공통으로 0단계와 1단계를 거칩니다.

| 단계 | 검사 | 실패 코드 |
|---|---|---|
| 0 | 64 KiB, BOM 제거, 문법, `signature` 형식, Ed25519 | `1002` / `1003` / `1004` |
| 1 | `check_mode`: 없음 → `full`. 문자열 `"full"`·`"minimal"`만 | 다른 문자열 `1006`, 문자열이 아니면 `1005` |
| `full` | 현행 코드 그대로: `license_ver` → v1 전체 필드 → 기간 | `1005` / `1006` / `1007` / `1008` |
| `minimal` 2 | `license_id` | `1005` |
| `minimal` 3 | `allowed_channels` | `1005` |
| `minimal` 4 | `validity` 형식과 순서 | `1005` |
| `minimal` 5 | 현재 시각 기준 기간 | `1007` / `1008` |

`minimal`의 표시 필드(판정하지 않음):
| 필드 | 값 |
|---|---|
| `project_name`(256), `license_type`(64), `solution`(128), `site_id`(256), `warning_notice`(4096) | v1 규칙(괄호 안 최대 바이트, 1바이트 이상, 제어문자 없음)에 맞는 문자열이면 그 값, 아니면 빈 문자열 |
| `license_ver` | 정수 1~2147483647이면 그 값, 아니면 0 |
| `validity.issued_at` | 시각 형식에 맞으면 그 값, 아니면 0. 출력(`InfoToJson`, `licensectl verify`)은 빈 문자열(1970년으로 보이지 않게) |
| 모르는 키 | 무시 |

Manager는 운영 중에 채널을 잡을 때마다(`Acquire`) 다음을 다시 봅니다. `minimal`도 같습니다.
- 기간이 지났거나 시작 전이면 `1008` / `1007`
- 라이선스에 없거나 상한이 0인 세부 항목은 `1011`
- 세부 항목이나 `total`이 모두 쓰이는 중이면 `1009`

### 7.5 SDK가 할 일
- **결과 코드만 보고 연결 여부를 정합니다.** `VerifyLicense`가 `Ok`면 사용하고, 아니면 `1000 + 코드`로 거부합니다. 로그에는 `ErrorName`, `detail`, `CheckModeName(fields.check_mode)`를 남깁니다. `check_mode`는 원문이 아니라 라이브러리가 돌려준 값을 봅니다.
- **값 적용(집행):** 안정 필드 값만 씁니다(`fields.total_channels`, `fields.channels`).
  - 세션 풀 크기는 `min(설정값, 세부 항목 상한)`입니다.
  - 동시 예약은 세부 항목 상한과 `total`을 둘 다 지킵니다.
  - 라이선스에 없는 세부 항목은 상한 0으로 봅니다.
  - Manager를 쓰면 라이브러리가 대신 합니다.
- **운영 중 기간 재판정:** JNI `connect` 방식은 연결할 때 한 번만 판정합니다(기존 미결 "갱신 방식"). Manager는 60초마다 다시 판정합니다.
- **표시 필드:** `minimal`에서는 빈 값일 수 있으므로 화면과 로그(`getSTTSessionInfo`)에서 처리합니다.
- **하지 말 일:**
  - 범위나 기간을 다시 검사하지 않습니다.
  - 안정 필드가 아닌 값(`license_type`, `license_ver` 포함)으로 동작을 정하지 않습니다.
  - 원문 JSON을 직접 해석해 판단하지 않습니다.
  - SDK 자체 검사(별도 만료 장치)는 기존 `.key` 경로(라이브러리가 `ParseError`를 낼 때)에만 씁니다.

### 7.6 보안
| 위협 | 결과 |
|---|---|
| 고객이 `full` 파일을 `minimal`로 고침 | `1004` |
| 고객이 설정으로 검사 수준을 낮춤 | 불가(설정이 없어짐, 라이선스가 정함). 현행보다 강해집니다 |
| `minimal` 파일로 만료·채널을 우회 | 불가. `minimal`에서도 기간과 채널을 집행합니다 |
| 다른 사이트의 파일을 재사용 | 현행 복제 문제(D24)와 같아 변화 없음 |
| SDK가 서명만 보고 연결을 허용 | 공개 API에 그런 함수가 없습니다(3.0.0). SDK가 서명 검증을 직접 구현하는 경우는 SDK 문서·수용 기준(05)으로 막습니다 |
| 발급자가 안정 필드의 의미를 바꾼 파일을 `minimal`로 발행 | 이전 SDK가 잘못 해석합니다. 발급 단계의 자기 검사와 SDK 최저 코어 시험으로만 막을 수 있습니다 |

## 8. 예시 (`check_mode: "minimal"`, 판정 시각 2026-10-19)

```json
{
  "check_mode": "minimal",
  "license_ver": 3,
  "license_id": "9b1c...",
  "allowed_channels": { "total": 16, "offline": { "sonastt": 8 }, "online": { "sonastt": 8 } },
  "validity": { "not_before": "2026-10-01T00:00:00Z", "not_after": "2027-09-30T14:59:59Z" },
  "host_binding": "srv-a",
  "signature": "..."
}
```

| # | 상황 | 결과 |
|---|---|---|
| M1 | 위 예시 그대로 | Ok, `check_mode` Minimal, `license_ver` 3. 이전 SDK는 `host_binding`을 **집행하지 않음** |
| M2 | `project_name`·`license_type`·`solution`·`site_id`·`warning_notice` 없음 | Ok, 표시 필드는 빈 값 |
| M3 | `site_id: 123`(정수), `license_type: "beta"` | Ok, `site_id`는 빈 값, `license_type`은 `"beta"`(표시만) |
| M4 | `check_mode: "Minimal"`(대문자) | `1006` |
| M5 | 고객이 `full` 파일의 `check_mode`를 `minimal`로 고침 | `1004` |
| M6 | `license_id` 없음, 또는 129바이트 | `1005` |
| M7 | `total: 200000` | `1005`(안정 필드 범위는 늘릴 수 없음) |
| M8 | `total: 16`인데 `offline.sonastt: 20` | `1005` |
| M9 | `allowed_channels.burst: 2` | `1005` |
| M10 | 모든 세부 항목이 0 | `1005` |
| M11 | `not_after`가 `"2027-01-01"`, `"…T00:00:00.000Z"`, `"…+09:00"` 형식 | `1005` |
| M12 | `not_before ≥ not_after` | `1005` |
| M13 | `not_before` 2026-11-01 | `1007` |
| M14 | `not_after` 2026-10-18T23:59:59Z | `1008` |
| M15 | 새 필드에 배열 | `1003`(`.key` 분기를 켰으면 기존 해독기로 넘어가지만 거기서도 거부) |
| M16 | 70 KiB | `1002` |
| M17 | 다른 키로 서명 | `1004` |
| M18 | 운영 중 라이선스에 없는 `online.sonaml` 요청 | `1011`(재시도해도 같음) |
| M19 | 운영 중 `offline.sonastt` 8개 사용 중, 또는 `total` 소진 | `1009`(잠시 뒤 재시도) |
| M20 | 운영 중 `not_after` 경과 | Manager는 다음 점검 때 `1008`. JNI `connect`만 쓰면 재시작 전까지 모름 |
| M21 | `license_ver: "3"`(문자열) 또는 없음 | Ok, `license_ver` 0 |
| M22 | `validity.issued_at` 없음 | Ok, `issued_at` 0, 출력은 빈 문자열 |
| M23 | `check_mode: 1`(정수) | `1005` |
| M24 | `check_mode: "full"` + `license_ver: 3` | `1006`(`full`은 버전을 봄) |
| M25 | `check_mode: "full"`을 명시한 v1 파일 | 키가 없는 파일과 결과가 같음 |

조용히 통과하는 것은 새 제한 필드(M1)와 표시 필드 누락·형식 차이(M2·M3·M21·M22)뿐입니다.

## 9. 구현 계획 (코어 3.0.0)

> **1단계 완료(2026-10-21):** 검증 모드 API·`VerifySignature`·공개 `ParseStandardFields`·설정 삭제, 하네스·SDK 문서 갱신, `VERSION 3.0.0`. 결과와 수치는 [remove-signature-mode.md](remove-signature-mode.md) §6. 아래 표에서 1단계에 해당하는 항목은 끝났고, 남은 것은 `check_mode` 판정(2단계)입니다.

SDK 첫 적용 착수 전에 끝내고 `sdk-v3.0.0`으로 발행합니다. 발급 서버의 발급 내용은 바뀌지 않습니다(`check_mode`를 넣지 않음 = `full`).

### 9.1 코어 (`core/`)
| 파일 | 변경 |
|---|---|
| `CMakeLists.txt` | `VERSION 3.0.0` |
| `include/stt_license/license.h` | `enum class CheckMode { Full = 0, Minimal = 1 }`, `const char* CheckModeName(CheckMode)`(`"full"`/`"minimal"`). `LicenseFields::check_mode`, `Info::check_mode` 추가. `VerifyMode`·`Verify`·`ParseVerifyMode`·`VerifyModeName` 삭제. 주석: `UnsupportedVersion` = "`license_ver` 또는 `check_mode` 값을 모름", `VerifySignature` = "연결 허용 판단에 쓰지 않는다", `ParseStandardFields`·`VerifyLicense` = "검사 수준은 `check_mode`가 정함", 헤더 첫머리의 사용 방식 (A) 설명 |
| `src/detail.h` | `LicenseData::check_mode`. `VerifyWith` 선언 삭제 |
| `src/license.cpp` | 아래 구조. 내부 `ParseStandardFields`가 `check_mode`를 옮겨 담음. (`VerifyWith`·`VerifyModeName`·`ParseVerifyMode` 삭제는 1단계에서 완료) |
| `src/manager.cpp` | `Snapshot`이 `check_mode`를 옮김. `InfoToJson`에 `"check_mode"` 추가, `minimal`이고 `issued_at`이 0이면 빈 문자열 |
| `fuzz/fuzz_parse.cpp` | `VerifyWith(…, Full, …)` → `VerifyLicenseWith` |
| `src/embedded_key.cpp.in` | (1단계 완료) `Verify`·`VerifySignature` 삭제, `VerifyLicense`가 `VerifyLicenseWith`를 직접 부름 |

`ExtractFields`의 구조(현행 `full` 코드는 손대지 않고 앞에 분기만 넣습니다):
```cpp
Error ExtractFields(const Value& root, LicenseData* out, std::string* detail) {
  if (const Value* cm = root.Find("check_mode")) {
    if (cm->type != Value::Type::String) return Fail("check_mode", "must be a string"), Error::InvalidField;
    if (cm->str == "minimal") return ExtractMinimal(root, out, detail);
    if (cm->str != "full") { /* detail: check_mode "<값>" is not supported */ return Error::UnsupportedVersion; }
  }
  // 이하 현행 코드 그대로 (d.check_mode = Full)
}
```
- `ExtractMinimal`은 현행 `Extractor`의 `Text`·`Int`·`Groups`·`Time`을 그대로 써서 안정 필드를 판정합니다. 규칙을 새로 쓰지 않습니다. "1 이상인 값이 하나 이상"과 `not_before < not_after` 검사는 작은 함수로 빼서 `full`과 같이 씁니다(판정 순서와 오류 코드는 그대로).
- 표시 필드 읽기는 **실패해도 `detail`을 건드리지 않는** 별도 도우미로 합니다. `Extractor::Fail`을 쓰면 성공한 판정에도 `detail`에 사유가 남습니다.
- `SignContract`는 바꾸지 않습니다. `check_mode`는 허용 키가 아니므로 계약에 넣으면 `unknown key`로 거부됩니다. `minimal` 발급은 실제로 필요해질 때 자기 검사(이 `ExtractMinimal` 그대로)와 함께 엽니다.
- `Error` 순번과 골든 파일은 바꾸지 않습니다. 골든 파일에는 `check_mode`가 없으므로 결과가 같습니다.

### 9.2 `licensectl`
- `verify` 요약 출력과 `-json`에 `check_mode`를 추가합니다. `minimal`이고 `issued_at`이 0이면 빈 문자열입니다.
- 발급 서버는 `-json`을 `Map`으로 읽으므로(`Licensectl.verify`) 키가 늘어도 깨지지 않습니다.

### 9.3 JNI 참조 구현·하네스 (`integration/`)
- `harness_jni.cpp`: (1단계 완료) `ReadVerifyMode`·설정 읽기 삭제, `VerifyLicense`로 판정, 상태 JSON의 `"mode"`·`"standard"` 삭제. 2단계: 상태 JSON에 `"check_mode"` 추가.
- `HarnessTest.java`: 모드 선택 시험을 다음으로 바꿉니다.
  - (1단계 완료) 설정 파일에 `--license-verify-mode=signature`가 있어도 만료 파일은 `1008`(설정이 검사를 낮추지 못함)
  - `minimal` 파일 연결 → 0, `check_mode` `minimal`
  - 기존 `.key` 분기(`ParseError`일 때만)는 그대로
- `cmake-consumer`: 바뀐 헤더로 빌드되는지 `check.sh`가 확인합니다.

### 9.4 시험
| 대상 | 시험 |
|---|---|
| `full` | 기존 시험 전부 그대로(골든 포함). `check_mode: "full"` 명시본이 같은 결과(M25). **A10 고정:** 모르는 최상위 키·`validity` 키·새 최상위 객체가 있는 v1 파일이 통과 |
| `minimal` | M1~M17, M21~M24. `SignRaw`로 만들고 키는 매번 새로 만듭니다. 성공한 판정에서 `detail`이 비어 있는지 |
| `check_mode` 판정 | 문자열 아님 `1005`, 모르는 값 `1006`, 대소문자 |
| Manager | `full` 파일을 `minimal` 파일로 교체해 다시 읽기, `Snapshot`·`InfoToJson`의 `check_mode`와 빈 표시 필드, M18~M20 |
| JNI 하네스 | §9.3 |
| 퍼저 | `minimal` 시드 1건 추가, 짧게 실행 |
| 발급 서버 | 새 `licensectl`로 `./gradlew test`, `integrationTest` 재실행(발급 결과에 `check_mode` 없음, 검증 화면 정상) |
| 패키지 | `scripts/check.sh` → `ALL CHECKS PASSED`, `make-sdk-release.sh --worktree --verify` |

### 9.5 문서
| 문서 | 변경 |
|---|---|
| `docs/sdk/02-integration.md` | (1단계 완료: §1 흐름, §2 API 표, §4 예, §5, §7, §9) 2단계: §2 API 표에 `CheckMode`·`fields.check_mode`, §4 `connect` 예(`VerifyLicense`, 로그에 `CheckModeName`), §5 "검증 수준 선택"을 "검사 수준은 라이선스가 정함"으로 재작성, §7 값 사용(안정 필드만, `signature` 모드 문구 삭제) |
| `docs/sdk/04-license-format.md` | §2 필드 표에 `check_mode`, §5 호환 규칙(안정 필드 표, `minimal` 판정, 추가형 규칙 A1~A10) |
| `docs/sdk/05-testing.md` | 수용 기준: 설정이 검사를 낮추지 못함, `minimal` 파일 연결, 표시 필드 빈 값 처리 |
| `docs/sdk/06-field-operations.md` | `1006`의 뜻(`check_mode` 포함), 파일 교체 전에 같은 코어 버전의 `licensectl verify`로 사전 점검 |
| `docs/sdk/01-build.md`, `03-stt-server.md`, `README.md`, `RELEASE_MANIFEST.md` | 검증 모드 언급 정리, 버전 3.0.0 |
| `docs/dev/STATUS.md`, `DECISIONS.md` | 구현 결과, D46 구현 표시 |

### 9.6 이번 범위가 아닌 것
- `minimal` 발급(코어 `SignContract`, 발급 서버 화면): 비추가형 변경이 실제로 필요해질 때.
- 추가형 새 필드와 SDK 최저 코어 시험(C4): 첫 새 필드를 넣을 때(§6.2). 그때마다 발급 서버의 y 버전을 올립니다.
- 발급 서버 검증 화면의 `check_mode` 표시(`verify.html` 키 목록): 다음 서버 판에 넣습니다. 지금 발급분은 모두 `full`입니다.

### 9.7 완료 조건
1. §9.4 시험 통과, 결과 수치를 STATUS §2에 기록
2. `sdk-v3.0.0` 발행, SDK 팀에 첫 적용 코어로 전달
3. SDK 팀이 2.0.2로 적용을 시작하지 않았는지 전달 전에 확인

## 10. 2026-10-21 검토에서 바로잡은 것

| # | 이전 계획 | 문제 | 고친 내용 |
|---|---|---|---|
| R1 | `Verify`의 `mode` 인자를 받되 무시 | 아무 일도 하지 않는 인자가 남고, 설정을 읽는 옛 예제가 조용히 동작 | 공개 헤더에서 삭제(§7.3) |
| R2 | 고칠 함수로 `ExtractFields`·`Verify`만 적음 | 공개 진입점 `ParseStandardFields`·`VerifyLicense`, Manager의 `Info`·`InfoToJson`이 빠짐 | 모두 `ExtractFields`를 거침을 확인하고 `Info::check_mode`, `InfoToJson` 추가(§9.1) |
| R3 | `minimal`의 `license_ver`·`issued_at` 값을 정하지 않음 | `LicenseFields::license_ver`는 `int`라 범위 밖 정수를 담지 못함. `issued_at`이 0이면 `1970-01-01T00:00:00Z`로 출력됨 | 1~2147483647이 아니면 0, `issued_at` 0은 빈 문자열로 출력(§7.4) |
| R4 | 표시 필드를 "느슨하게 읽음" | 현행 `Extractor::Fail`을 쓰면 성공한 판정에도 `detail`이 남음 | `detail`을 쓰지 않는 도우미, 시험 추가(§9.1, §9.4) |
| R5 | 추가형 진화의 증명을 "이전 코어로 검증" | 기준 코어가 모호. 2.0.2는 SDK에 들어가지 않음 | 기준 = SDK 최저 코어(3.0.0), 태그에서 빌드(§6.2) |
| R6 | 발급 쪽 변경 목록 | `KeyRank`, `licensectl` 출력·대장, `LicenseRecord`·`LicenseRepository`, 감사 기록, `verify.html`이 빠짐. 마이그레이션 방법이 없음 | 목록 보완, `license_guard()`는 새 마이그레이션에서 `CREATE OR REPLACE`(§6.2) |
| R7 | 추가형이 기대는 "모르는 키 무시"가 암묵적 | 이후 코어가 엄격해지면 추가형 발급분이 깨짐 | 규칙 A10과 코어 시험으로 고정 |
| R8 | 공통 불변에 문법 제약이 요약만 있음 | 새 필드에 목록·음수·소수를 쓰면 `1003` | §3에 키 문법·깊이·값 종류, A5 보완 |
| R9 | 안정 필드 규칙이 코드보다 느슨하게 적힘 | `license_id` 제어문자 금지, 시각의 1970년 이상·실제 날짜 누락 | 코드와 같게 적음(§7.2) |
| R10 | `license_type` 결정 대기 문구 | 결정 2로 해소 | 삭제. SDK가 유형으로 동작을 나누지 않는다고 적음(§1, §7.5) |
| R11 | 버전 "코어 2.1.0" | 공개 헤더 API 삭제는 x를 올리는 변경(`docs/release/sdk.md` 버전 규칙) | 3.0.0 |
| R12 | `VerifySignature`를 "값 읽기 용도"로 남김 | 서명만 보고 연결을 허용하는 길이 그대로 남음 | 삭제(사용자 요청 2026-10-21). 값 읽기는 `VerifyLicense`를 통과한 `VerifiedLicense`로 |

## 11. 검토 후 버린 안

| 안 | 버린 이유 |
|---|---|
| `compat_ver`(정수, "v1 전체 규칙을 지킴" 선언) | 사용자 의도(표시 필드 변경까지 허용)보다 범위가 좁습니다. 같은 문제를 두 장치로 풀 필요가 없습니다 |
| 추가 발행분은 순수 `signature` | 라이브러리가 기간과 채널을 보지 않습니다(§7.1) |
| 설정이 라이선스보다 우선, 또는 설정이 최소 요구 수준 | 설정으로 검사를 낮추는 우회가 남거나, `minimal` 파일을 받지 못해 목표와 충돌합니다 |
| `Verify`의 `mode` 인자를 받되 무시 | §10 R1 |
| `check_mode`에 `full`만 정의 | "SDK는 항상 `full`"과 효과가 같아 의미가 없습니다 |
| 버전과 무관하게 v1 필드만 보고 통과 | 의미가 바뀐 형식도 통과해 실패 폐쇄를 잃습니다 |
| 키 단위 `critical` 목록 | 문법에 배열이 없고, 필요한 것보다 표현력이 큽니다 |
| 안 가(`check_mode` 없음, SDK 항상 `full`, 설정 폐지) | 가장 단순하지만 비추가형 변경의 비상구가 없습니다. 비상구는 첫 적용 코어에만 넣을 수 있습니다. 사용자가 안 나를 선택(2026-10-21) |

## 12. 검토 이력

| 날짜 | 내용 |
|---|---|
| 2026-10-19 | 처음 질문을 하위 호환으로 해석해 `compat_ver`를 제안 → 사용자 정정(전방 호환) |
| 2026-10-19 | "새 SDK에게 필수 집행을 알리면서 이전 SDK도 받게 할 수 없다"는 판단을 **정정**(필드 존재 자체가 서명되므로 "있으면 집행"으로 충분) |
| 2026-10-19 | "v2 이후 v1 발급 가능"을 "원리상 가능, 지금 코드로는 아님"으로 **보완**(모든 계층이 v1에 고정) |
| 2026-10-19 | 정적 라이브러리 SDK의 모드별 영향 검토 → `signature` 모드 오해석과 설정 우회 발견 |
| 2026-10-19 | 사용자 의도(체크 옵션으로 재빌드 회피) 반영 → `check_mode`·안정 필드·`minimal` 설계, 추가형 진화와 결합 |
| 2026-10-21 | 사용자 결정(안 나, 안정 필드에서 `license_type` 제외, 이름, 첫 적용 전) → D46. 코드 대조 검토로 R1~R10을 고치고 구현 계획(§9) 확정 |
| 2026-10-21 | 사용자 요청으로 `signature` 모드(`VerifySignature` 포함) 삭제를 1단계로 구현. 버전 3.0.0으로 정정(R11·R12) |

상세한 검토 원문은 로컬 계획서 `work_claude/license_implementation_plan.md` §20에 있습니다(git 무시).

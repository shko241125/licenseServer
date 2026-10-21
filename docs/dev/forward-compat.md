# 라이선스 전방 호환: 결정과 구현 계획 (코어 3.0.0)

- **상태:** 결정 완료(2026-10-21, D46). 구현 계획 확정. **1단계(`signature` 모드 삭제) 구현 완료**(2026-10-21, [remove-signature-mode.md](remove-signature-mode.md)), 2단계(`check_mode` 판정)는 구현 전. 처음 검토는 2026-10-19.
- **궁극 목표(사용자, 2026-10-21):** 발급 서버를 추가형으로 진화·확장해 ① 형식이 바뀐 뒤에도 **이전 형식 판으로 발급·검사**(하위 버전 발급)하고 ② `check_mode`로 **전방 호환**(이전 SDK가 새 형식을 받음)을 달성한다. 발급 서버 쪽 설계는 §13, 단계별 순서는 §13.7.
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

1. **코어 2.0.2(마지막 발행판) 그대로 버전(`license_ver`)을 올리면 이전 SDK와 호환되지 않습니다.**
   - `full` 모드와 Manager는 항상 `1006`으로 거부합니다.
   - `signature` 모드는 버전을 보지 않습니다. 그래서 비호환 형식도 통과하고, SDK가 v1 의미로 **잘못 해석합니다.** 이 모드는 3.0.0에서 삭제했습니다(1단계).
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

## 4. 검토 출발점: 코어 2.0.2(마지막 발행판)의 동작

3.0.0에서 `signature` 모드를 지웠으므로 이 절의 `signature` 열은 2.0.2의 기록입니다. 3.0.0 이후 판정은 §7.4입니다.

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
| `signature` 모드의 조용한 오해석 | 버전을 보지 않으므로 비호환 형식도 통과합니다. 운영 기본값이 `signature`라 영향 범위가 가장 넓습니다 | 연결 허용 판단을 `VerifyLicense`·Manager로 통일(§7.3). **1단계에서 해결** |
| 설정으로 검사를 끌 수 있음 | 검증 모드는 고객 현장의 SDK 설정 파일(`--license-verify-mode`)에서 읽고, 없으면 `signature`입니다(D15). D43의 "`trial`·`dev` 서버는 `full`"을 설정 한 줄로 끌 수 있습니다. 끄면 라이브러리는 만료를 보지 않습니다 | 검사 수준을 라이브러리·서명된 라이선스가 정하고, 설정을 없앰(§7.3). **1단계에서 해결** |

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
- 시험 단계는 첫 추가 필드를 넣는 작업에서 `check.sh`에 만듭니다. 기준 코어는 SDK 발행 태그(`sdk-v3.0.0`)에서 빌드합니다(코어 전용 태그는 없음).

### 6.3 비추가형 변경일 때 이전 형식(v1)으로 발급하기
원리상 가능합니다. 이전 SDK는 파일 내용과 공개키만 봅니다. 그러나 **지금 코드로 되는 것은 아닙니다.** 코어 허용 키, `Contract.LICENSE_VER`, 폼, 검증, 장부, DB `CHECK (license_ver = 1)`이 모두 v1에 고정되어 있습니다. 버전을 올리는 작업을 할 때 다음을 요구사항으로 넣어야 합니다.
- 같은 서명 키를 씁니다.
- 형식별 허용 키와 판정을 둡니다. v1 판정 코드는 고치지 않고 그대로 둡니다.
- SDK 최저 코어로 검증하는 시험을 둡니다.
- 발급 서버는 폼에서 형식 판을 고르게 합니다(사이트별 기록 방식은 §13.6 미결 1). 폼, 검증, DB 제약도 판별로 둡니다. 설계는 §13.

이 비용은 D37(이전 형식 공존)에서 한 번 들였다가 D42에서 걷어낸 것과 같은 종류입니다. `minimal`(§7)은 안정 필드를 지키는 변경에 한해 이 비용을 덜어 줍니다. 안정 필드가 바뀌는 판, 또는 이전 SDK에서 표시 필드까지 정확해야 하는 경우에는 하위 판 발급이 계속 필요합니다(§13.2).

### 6.4 새 SDK가 이전 라이선스를 받는 방향
새 코어는 `license_ver` 1과 `check_mode`가 없는 파일을 **영구히** 받아야 합니다. 운영 라이선스는 영구(9999년)라서 현장에 계속 남기 때문입니다. D42("이전 형식 지원 제거")는 SDK 적용 전 형식에 대한 결정이며, 적용 이후의 v1에는 해당하지 않습니다.

## 7. 전략 2: 체크 옵션 `check_mode` (비상구)

### 7.1 값
| 값 | 라이브러리 판정 | 쓰는 때 |
|---|---|---|
| 없음 또는 `"full"` | 현행 `full`: 버전 1, v1 전체 필드, 기간 | 처음 발급분. 추가형 발급분. **지금 발급분 전부(키 없음)** |
| `"minimal"` | 이 코어가 **모르는** `license_ver`면 서명 + 안정 필드 + 기간만(나머지 필드는 판정하지 않음). **아는** `license_ver`면 그 판의 전체 규칙(R13, 3.0.0에서 아는 판은 1) | 판 2 이상의 발급분 중 받는 SDK가 그 판을 모를 수 있는 것(§13.8, I3 결정 대기). 안정 필드 유지가 발급 조건 |
| 그 밖의 문자열 | `1006` | 미래의 다른 수준에 대비해 실패 폐쇄 |
| 문자열이 아님 | `1005` | — |

- 값은 대소문자를 구분합니다(`"Minimal"`은 `1006`).
- 추가 발행분을 순수 `signature`로 검사하지 않는 이유가 있습니다. 순수 `signature`면 기간과 채널을 라이브러리가 보지 않아, 만료와 동시 세션 제한을 SDK가 따로 구현해야 하기 때문입니다(D17의 별도 만료 장치 문제가 그대로 남음).
- `minimal`의 집행 강도는 `full`과 같습니다. 거부 사유가 서명·채널·기간이라는 점도 같습니다.
- `minimal`은 **그 형식을 모르는 검증기가 해야 할 최소 판정**입니다. 판정 함수(`ExtractMinimal`)는 3.0.0 이후 고치지 않습니다.
- 그 `license_ver`를 아는 코어는 `check_mode`와 관계없이 **그 판의 전체 규칙**으로 판정합니다(R13). 그래서 `minimal`은 새 코어에게는 약해지지 않고, 아는 판은 코어마다 판정이 같습니다. 3.0.0에서 아는 판은 1이므로 `minimal` + `license_ver: 1`은 v1 전체 규칙입니다(그런 파일은 발급하지 않음, §13.3 I3).

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
- `LicenseFields::check_mode`·`Info::check_mode`는 **실제로 적용한 판정 수준**입니다(파일에 적힌 값이 아님). 아는 판이면 `minimal` 파일도 `Full`, 모르는 판의 `minimal` 파일만 `Minimal`입니다(R13, R17).
- 연결 허용 판단은 `VerifyLicense`(또는 Manager)로만 합니다. `VerifyLicense`·내부 `ParseStandardFields`·Manager·`licensectl verify`는 모두 `ExtractFields`를 거치므로 `check_mode`가 한 곳에서 적용됩니다.
- `VerifySignature`도 삭제했습니다. 서명만 보고 받아 주는 함수가 남으면 모드를 지운 효과가 없기 때문입니다(2026-10-21 사용자 요청). 서명 단계는 내부(`VerifySignatureWith`, `detail.h`)에만 남습니다. 기존 `.key` 분기(`ParseError`일 때만)는 그대로입니다(D16).

### 7.4 판정 순서
실패하면 그 단계에서 멈춥니다. `full`·`minimal` 공통으로 0단계와 1단계를 거칩니다.

| 단계 | 검사 | 실패 코드 |
|---|---|---|
| 0 | 64 KiB, BOM 제거, 문법, `signature` 형식, Ed25519 | `1002` / `1003` / `1004` |
| 1 | `check_mode`: 없음 → `full`. 문자열 `"full"`·`"minimal"`만 | 다른 문자열 `1006`, 문자열이 아니면 `1005` |
| 1-2 | `minimal`인데 `license_ver`가 이 코어가 아는 판(3.0.0: 정수 1)이면 `full` 경로로 | — |
| `full` | 현행 코드 그대로: `license_ver` → v1 전체 필드 → 기간. 1-2단계로 온 파일도 같음 | `1005` / `1006` / `1007` / `1008` |
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

판정 예시라 `issued_at` 등은 생략했습니다. 발급 서버가 만드는 파일에는 항상 들어갑니다(§13.3 I1). 코어는 3.0.0(아는 판 = 1)입니다.

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
| M1 | 위 예시 그대로 | Ok, 적용 수준 `Minimal`(판 3을 모름), `license_ver` 3. 이전 SDK는 `host_binding`을 **집행하지 않음** |
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
| M26 | `check_mode: "minimal"` + `license_ver: 1` + `site_id: 123` | `1005`(아는 판이므로 v1 전체 규칙, R13) |

조용히 통과하는 것은 새 제한 필드(M1)와 표시 필드 누락·형식 차이(M2·M3·M21·M22)뿐입니다.

## 9. 구현 계획 (코어 3.0.0)

> **1단계 완료(2026-10-21):** 검증 모드 API·`VerifySignature`·공개 `ParseStandardFields`·설정 삭제, 하네스·SDK 문서 갱신, `VERSION 3.0.0`. 결과와 수치는 [remove-signature-mode.md](remove-signature-mode.md) §6. 아래 표에서 1단계에 해당하는 항목은 끝났고, 남은 것은 `check_mode` 판정(2단계)입니다.

SDK 첫 적용 착수 전에 끝내고 `sdk-v3.0.0`으로 발행합니다. 발급 서버의 발급 내용은 바뀌지 않습니다(`check_mode`를 넣지 않음 = `full`). 검증 화면 표시를 바꾸므로 발급 서버도 y 버전을 올려 발행합니다(D44).

### 9.1 코어 (`core/`)
| 파일 | 변경 |
|---|---|
| `CMakeLists.txt` | (1단계 완료) `VERSION 3.0.0` |
| `include/stt_license/license.h` | `enum class CheckMode { Full = 0, Minimal = 1 }`, `const char* CheckModeName(CheckMode)`(`"full"`/`"minimal"`). `LicenseFields::check_mode`, `Info::check_mode` 추가(적용한 수준, R17). 주석: `UnsupportedVersion` = "`license_ver`(`check_mode` 없음·`full`일 때) 또는 `check_mode` 값을 모름", `VerifyLicense` = 판정 순서에 `check_mode`. (모드 API·`VerifySignature`·공개 `ParseStandardFields` 삭제는 1단계 완료) |
| `src/detail.h` | `LicenseData::check_mode`, `ExtractMinimal` 선언(내부, 판 2 발급의 자기 검증 I5가 직접 부름). (`VerifyWith` 삭제는 1단계 완료) |
| `src/license.cpp` | 아래 구조. 내부 `ParseStandardFields`가 `check_mode`를 옮겨 담음. (`VerifyWith`·`VerifyModeName`·`ParseVerifyMode` 삭제는 1단계에서 완료) |
| `src/manager.cpp` | `Snapshot`이 `check_mode`를 옮김. `InfoToJson`에 `"check_mode"` 추가, `minimal`이고 `issued_at`이 0이면 빈 문자열 |
| `fuzz/fuzz_parse.cpp` | (1단계 완료) `VerifyLicenseWith`. 2단계: `minimal` 경로의 불변식(통과하면 안정 필드 규칙 만족) |
| `src/embedded_key.cpp.in` | (1단계 완료) `Verify`·`VerifySignature` 삭제, `VerifyLicense`가 `VerifyLicenseWith`를 직접 부름 |

`ExtractFields`의 구조(현행 `full` 코드는 손대지 않고 앞에 분기만 넣습니다):
```cpp
Error ExtractFields(const Value& root, LicenseData* out, std::string* detail) {
  const Value* lv = root.Find("license_ver");
  const bool known_ver = lv && lv->type == Value::Type::Int && lv->num == kLicenseVer;  // 아는 판(3.0.0: 1)
  if (const Value* cm = root.Find("check_mode")) {
    if (cm->type != Value::Type::String) return Fail("check_mode", "must be a string"), Error::InvalidField;
    if (cm->str == "minimal" && !known_ver) return ExtractMinimal(root, out, detail);  // 모르는 판만 최소 판정
    if (cm->str != "full" && cm->str != "minimal") { /* check_mode "<값>" is not supported */ return Error::UnsupportedVersion; }
  }
  // 이하 현행 코드 그대로 (d.check_mode = Full: 적용한 수준)
}
```
- `ExtractMinimal`은 현행 `Extractor`의 `Text`·`Int`·`Groups`·`Time`을 그대로 써서 안정 필드를 판정합니다. 규칙을 새로 쓰지 않습니다. "1 이상인 값이 하나 이상"과 `not_before < not_after` 검사는 작은 함수로 빼서 `full`과 같이 씁니다(판정 순서와 오류 코드는 그대로).
- 표시 필드 읽기는 **실패해도 `detail`을 건드리지 않는** 별도 도우미로 합니다. `Extractor::Fail`을 쓰면 성공한 판정에도 `detail`에 사유가 남습니다.
- `SignContract`는 바꾸지 않습니다. `check_mode`는 허용 키가 아니므로 계약에 넣으면 `unknown key`로 거부됩니다(I3). `minimal` 발급은 판 2 때 엽니다(§13.4). 그때 자기 검증은 `ExtractMinimal`을 **직접** 불러야 합니다. 판 2를 아는 코어에서는 `ExtractFields` 경로가 R13 때문에 판 2 규칙으로 가므로, 그 경로만으로는 이전 SDK의 최소 판정을 확인하지 못합니다(R18).
- `Error` 순번과 골든 파일은 바꾸지 않습니다. 골든 파일에는 `check_mode`가 없으므로 결과가 같습니다.

### 9.2 `licensectl`
- `verify` 요약 출력과 `-json`에 `check_mode`(적용한 수준)를 추가합니다. `license_ver`는 지금도 출력합니다. `minimal`이고 `issued_at`이 0이면 빈 문자열입니다.
- 발급 서버는 `-json`을 `Map`으로 읽으므로(`Licensectl.verify`) 키가 늘어도 깨지지 않습니다.

### 9.3 JNI 참조 구현·하네스 (`integration/`)
- `harness_jni.cpp`: (1단계 완료) `ReadVerifyMode`·설정 읽기 삭제, `VerifyLicense`로 판정, 상태 JSON의 `"mode"`·`"standard"` 삭제. 2단계: 상태 JSON에 `"check_mode"` 추가.
- `HarnessTest.java`: 모드 선택 시험을 다음으로 바꿉니다.
  - (1단계 완료) 설정 파일에 `--license-verify-mode=signature`가 있어도 만료 파일은 `1008`(설정이 검사를 낮추지 못함)
  - 판 2(이 코어가 모르는 판) `minimal` 파일 연결 → 0, 적용 수준 `minimal`. 판 1 `minimal` 파일 → v1 전체 규칙(R13)
  - `minimal` 시험 파일은 `licensectl`로 만들 수 없습니다(`SignContract`가 거부, I3). 하네스 Java 시험이 JDK 내장 Ed25519로 직접 서명해 만듭니다(키 이름 정렬·공백 없는 JSON. 고정 내용을 ASCII·정수만으로 두면 RFC 8785 정규형과 같음)
  - 기존 `.key` 분기(`ParseError`일 때만)는 그대로
- `cmake-consumer`: 바뀐 헤더로 빌드되는지 `check.sh`가 확인합니다.

### 9.4 시험
| 대상 | 시험 |
|---|---|
| `full` | 기존 시험 전부 그대로(골든 포함). `check_mode: "full"` 명시본이 같은 결과(M25). **A10 고정:** 모르는 최상위 키·`validity` 키·새 최상위 객체가 있는 v1 파일이 통과 |
| `minimal` | M1~M17, M21~M24, M26(아는 판 + `minimal` = v1 전체 규칙). `SignRaw`로 만들고 키는 매번 새로 만듭니다. 성공한 판정에서 `detail`이 비어 있는지 |
| `check_mode` 판정 | 문자열 아님 `1005`, 모르는 값 `1006`, 대소문자 |
| Manager | `full` 파일을 판 2 `minimal` 파일로 교체해 다시 읽기, `Snapshot`·`InfoToJson`의 `check_mode`와 빈 표시 필드, M18~M20 |
| JNI 하네스 | §9.3 |
| 퍼저 | 판 2 `minimal` 시드 1건 추가, 짧게 실행 |
| 발급 서버 | 새 `licensectl`로 `./gradlew test`, `integrationTest` 재실행. 추가: 발급 파일의 `validity.issued_at`이 DB `issued_at`과 같고(I1), `check_mode` 키가 없음(I3). 검증 화면에 `license_ver`·`check_mode` 표시 |
| 패키지 | `scripts/check.sh` → `ALL CHECKS PASSED`, `make-sdk-release.sh --worktree --verify` |

### 9.5 문서
| 문서 | 변경 |
|---|---|
| `docs/sdk/02-integration.md` | (1단계 완료: §1 흐름, §2 API 표, §4 예, §5, §7, §9) 2단계: §2 API 표에 `CheckMode`·`fields.check_mode`, §4 로그에 `CheckModeName(fields.check_mode)`, §5에 `check_mode`가 정하는 수준, §7에 "안정 필드만 동작 판단에 씀" |
| `docs/sdk/04-license-format.md` | §2 필드 표에 `check_mode`, §5 호환 규칙(안정 필드 표, `minimal` 판정, 추가형 규칙 A1~A10) |
| `docs/sdk/05-testing.md` | 수용 기준: `minimal` 파일 연결, 표시 필드 빈 값 처리. (설정이 검사를 낮추지 못함은 1단계 완료) |
| `docs/sdk/06-field-operations.md` | `1006`의 뜻(`check_mode` 포함), 파일 교체 전에 같은 코어 버전의 `licensectl verify`로 사전 점검 |
| `docs/sdk/03-stt-server.md`, `README.md` | `1006`의 뜻, 변경 이력 3.0.0에 `check_mode`. (검증 모드 언급 정리·버전 표기는 1단계 완료) |
| `docs/sdk/RELEASE_MANIFEST.md`, `docs/release/sdk.md` | 3.0.0 발행 기록 |
| `license-server/README.md`, `docs/release/license-server.md` | 검증 화면의 `license_ver`·`check_mode`, 서버 발행 기록 |
| `docs/dev/STATUS.md`, `DECISIONS.md` | 구현 결과, D46 구현 표시 |

### 9.6 이번 범위가 아닌 것 (단계는 §13.7)
- `minimal` 발급과 하위 버전 발급(코어 `SignContract`의 판별 규칙, 발급 서버 화면·DB): 판 2가 처음 생길 때(§13.4, 4단계). 판 1 파일에는 `check_mode`를 넣지 않으므로(I3) 그 전에는 발급할 일이 없습니다.
- 추가형 새 필드와 SDK 최저 코어 시험(C4·I8): 첫 새 필드를 넣을 때(§6.2, 3단계). 그때마다 발급 서버의 y 버전을 올립니다.
- 발급 서버 검증 화면의 `license_ver`·`check_mode` 표시는 **2단계에 넣습니다**(§13.7. 이전 계획의 "다음 서버 판"에서 앞당김).

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
| R13 | `minimal`은 버전과 무관하게 최소 판정 | 같은 파일이 아는 판인데도 코어마다 판정이 달라질 수 있음(이후 코어가 그 판 규칙을 보면 더 엄격) | 아는 판이면 그 판의 전체 규칙, 모르는 판만 최소 판정(§7.1, §7.4 1-2단계, M26) |
| R14 | `minimal`을 언제 넣는지 사람이 고르는 것처럼 적힘 | 같은 형식에 수준이 둘이면 발급 실수가 생김 | 형식 판에서 자동: v1은 키 없음, 2 이상은 `"minimal"`(§13.3 I3). DB 제약으로도 고정 |
| R15 | `issued_at`을 `minimal`에서 느슨하게 읽는 것이 "없어도 되는 필드"로 읽힐 수 있음 | 검증 규칙과 발급 규칙이 섞임 | 발급은 모든 판·수준에서 **필수**(§13.3 I1), 검증만 `minimal`에서 느슨(표시 필드) |
| R16 | 하위 버전 발급(§6.3)이 요구사항 목록뿐 | 판 선택 기준·갱신·검사·DB 제약이 없음 | §13 |
| R17 | `fields.check_mode`가 파일의 값인지 적용한 수준인지 정하지 않음 | R13 이후 둘이 다를 수 있음(판 1 `minimal` 파일은 `Full`로 판정) | 적용한 수준으로 정의(§7.3). M1 문구 수정 |
| R18 | 판 2 발급의 자기 검증을 `ExtractMinimal`로 한다고만 적음 | 판 2를 아는 발급 코어에서는 `ExtractFields`가 판 2 규칙으로 가서 최소 판정을 타지 않음 | `ExtractMinimal`을 직접 호출(§9.1, I5) |
| R19 | 1단계 완료 항목이 §9 할 일 표에 그대로 남음, `VerifySignature` 주석을 고친다는 행 | 이미 삭제한 함수를 고칠 대상으로 적음 | 완료 표시, 남은 일만 적음(§9.1·§9.5) |
| R20 | 기준 코어 태그 `core-v3.0.0` | 저장소에 코어 전용 태그가 없음(발행 태그는 `sdk-v*`) | `sdk-v3.0.0`(§6.2) |
| R21 | `minimal` 시험 파일 만드는 방법이 없음 | `licensectl`은 `check_mode`를 거부(I3), `check.sh`에는 서명 도구가 없음 | 하네스 Java가 JDK Ed25519로 직접 서명(§9.3). 코어 시험은 `SignRaw` |
| R23 | "아는 판"을 현장에서 알아낼 방법이 없음 | 아는 판은 코어에 컴파일된 상수(`kLicenseVer`)뿐이고, 라이브러리 버전·아는 판을 밖으로 알려 주는 API가 없음. 발급자는 사이트 SDK 코어를 기억에 의존해야 함 | 3.0.0에 조회 함수를 넣는 안(§13.8 D4). 첫 적용 코어에만 넣을 수 있음 |
| R22 | I8이 "통과"의 기준을 정하지 않음 | `licensectl verify`는 기간 전·후면 종료 코드 1이지만 형식은 정상(`result` `OK`) | I8은 `result == "OK"`(서명·형식·필드)로 판정하고 기간은 `state`로 따로 표시(§13.3) |

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
| 2026-10-21 | 궁극 목표(하위 버전 발급 + `check_mode` 전방 호환) 기준 재검토 → `minimal` 판정 규칙 보완(R13), 발급 규칙(R14·R15), 발급 서버 설계 §13(R16) |
| 2026-10-21 | 문서 전체 재검토 → R17~R22(적용 수준 정의, 판 2 자기 검증 경로, 1단계 완료 항목 정리, 태그 이름, `minimal` 시험 파일, I8 통과 기준), 2.0.2 기록과 현재 판정 구분 |

상세한 검토 원문은 로컬 계획서 `work_claude/license_implementation_plan.md` §20에 있습니다(git 무시).

## 13. 발급 서버: 하위 버전 발급과 `minimal` 발급 (2026-10-21 검토)

### 13.1 용어와 목표
| 용어 | 뜻 |
|---|---|
| 형식 판 | `license_ver` 값. 지금은 1뿐. 비추가형 변경(§6.3)이 생길 때 2, 3…을 만듭니다 |
| 하위 버전 발급 | 판 n이 생긴 뒤에도 사이트에 맞춰 판 k(k < n) 형식으로 발급·검사 |
| `minimal` 발급 | 판 2 이상의 파일에 `check_mode: "minimal"`을 넣어, 그 판을 모르는 SDK(최저 코어 3.0.0)도 받게 함 |
| SDK 최저 코어 | 현장 SDK에 들어간 가장 오래된 코어(첫 적용 3.0.0) |

- 판 1 안의 추가형 변경(§6)에는 둘 다 필요 없습니다. 이전 SDK가 `full`로 받습니다.
- 둘 다 **판 2가 처음 생길 때** 필요해집니다. 지금 코드에 넣을 것은 그 준비(§13.7의 2단계)뿐입니다.

### 13.2 사이트에 무엇을 발급하나
라이선스 파일 한 장은 한 사이트에 갑니다. 그래서 기준은 **그 사이트의 STT 서버들에 들어간 SDK 코어**입니다.

| 사이트 상황 | 발급 | 이유 |
|---|---|---|
| 모든 서버의 SDK 코어가 판 n을 앎 | 판 n(안 A: `minimal`, 안 C: 키 없음, §13.8 D3) | 아는 판이라 어느 쪽이든 판 n 전체 규칙으로 판정(R13) |
| SDK 코어가 판 n을 모름, 판 n의 변경이 안정 필드를 지킴 | 판 n `minimal`, 또는 하위 판 | `minimal`이면 이전 SDK는 안정 필드만 판정. 새 필드는 이전 SDK에서 집행되지 않음(A8) |
| 서버마다 SDK 코어가 다름(단계적 교체 중), 같은 파일을 함께 씀 | 판 n `minimal` | 한 파일로 두 종류 SDK가 모두 받음. **`minimal`이 꼭 필요한 경우** |
| 판 n이 안정 필드를 바꿈 | **하위 판**(그 사이트 SDK가 아는 판) | `minimal`로도 이전 SDK가 받지 못함(§7.6 마지막 행). 아니면 SDK 재빌드 |
| 이전 SDK에서 표시 필드까지 정확히 보여야 함 | 하위 판 | `minimal`은 이전 SDK에서 표시 필드가 빈 값일 수 있음(M2·M3) |

사이트의 SDK 코어를 기록하는 장부는 지금 없습니다. 판 2를 만들 때 정합니다(§13.6 미결 1).

### 13.3 발급 불변식 (모든 판·수준 공통)
| # | 규칙 | 지금 상태(코드 확인) |
|---|---|---|
| I1 | **`validity.issued_at`은 항상 넣습니다.** 값은 발급 서버의 발급 시각(UTC, 초 단위)이고 DB `license.issued_at`과 같은 값입니다 | **이미 보장됨(여러 층).** 서버 `ContractValidator`가 `now`(초 단위)를 넣고 `Contract.toJson`이 항상 씀 → 코어 `SignContract`가 없으면 채움 → 판 1 `full` 판정이 필수로 요구해 자기 검증에서 막힘. DB 열 `NOT NULL`(V1). 시험 `IssueFlowIT`(`validity` 키가 정확히 `issued_at`·`not_before`·`not_after`). 판 2 이상에서도 같게: 그 판의 `SignContract`가 없으면 채우고, 그 판의 전체 규칙이 필수로 요구 |
| I2 | `license_id`는 서버가 만든 32자리 16진수 | 이미 보장됨(`IssueService`) |
| I3 | `check_mode`는 사람이 따로 고르지 않고 **정해진 규칙으로** 넣습니다. 판 1은 키 없음(= `full`). `"full"`을 명시한 파일은 발급하지 않습니다(없음과 같음). 판 2 이상의 규칙은 **결정 대기**: 안 A(항상 `"minimal"`) / 안 C(사이트 기록상 그 사이트의 모든 SDK가 그 판을 알면 키 없음, 아니면 `"minimal"`). §13.8 | 판 1: 이미 보장됨(`check_mode`는 허용 키가 아니라 계약에 넣으면 거부). 판 2: 그때 구현 |
| I4 | 화면에서 사람이 고르는 것은 **형식 판 하나**입니다. `check_mode`를 따로 고르는 칸은 두지 않습니다 | 판 2 때 구현 |
| I5 | 판 2 이상의 자기 검증은 두 가지를 모두 통과해야 합니다: (a) 발급 코어의 그 판 전체 규칙(`ExtractFields`) (b) 고정된 최소 판정 `ExtractMinimal`을 **직접 호출**(R18) | 판 2 때 구현. (b)는 3.0.0의 함수를 그대로 씀 |
| I6 | 하위 판의 발급 코드는 고치지 않습니다(판 1의 `SignContract` 규칙·`Contract.toJson`·`ContractValidator`). 같은 키·ID·시각으로 발급한 결과 바이트를 고정 시험으로 둡니다 | 판 2 때 시험 추가(기존 골든 파일은 그대로) |
| I7 | 갱신 발급(`copyOf`)은 **원본의 판을 그대로** 씁니다. 판을 바꾸려면 화면에서 따로 고르고, 바뀐다는 경고를 보여 줍니다 | 판 2 때 구현 |
| I8 | 발급 직후 검증에 **SDK 최저 코어 판정**을 더합니다. 발급 서버 이미지에 SDK에 나간 코어별 `licensectl`(태그에서 빌드)을 넣고, 발급 파일이 대상 코어에서 통과해야 발급을 끝냅니다(지금의 `VERIFY_MISMATCH`처럼 실패 처리). 통과 기준은 `verify -json`의 `result == "OK"`이고, 기간(`state`)은 판정하지 않고 표시만 합니다(시작일이 미래인 발급도 정상, R22) | 첫 추가 필드(C4) 또는 판 2 때 구현 |

`issued_at`을 안정 필드로 올리지는 않습니다. SDK가 동작 판단에 쓰지 않는 필드라서, 고정하면 형식만 영구히 묶이고 집행상 얻는 것이 없습니다. "발급은 필수(I1), `minimal` 검증은 표시만"으로 나눕니다(R15).

### 13.4 발급 서버 변경 (판 2를 만들 때)
| 계층 | 변경 |
|---|---|
| 코어 발급 | `SignContract`가 계약의 `license_ver`로 판별 규칙을 고름. 판 1 경로는 그대로(I6). 판 2 이상은 허용 키에 `check_mode`를 넣고 `"minimal"`만 허용, 자기 검증 I5 |
| 코어 판정 | 아는 판 목록에 2를 더함(R13: 판 2 전체 규칙). 판 1 판정 코드는 그대로 |
| `licensectl` | `verify` 출력에 `license_ver`·`check_mode`·적용한 규칙(판 n 전체 / 최소). 대장 행에 `check_mode` |
| `Contract` | `licenseVer`·`checkMode`를 필드로(지금은 상수 `LICENSE_VER`). 판별 직렬화(판 1 직렬화는 그대로) |
| `ContractValidator` | 판별 규칙, 경계값 일치 시험(`ValidationParityTest`)을 판마다 |
| 화면 | `form.html`: 판이 둘 이상일 때만 "형식 판" 선택(기본값: 갱신이면 원본 판, 새 발급이면 그 `site_id`의 마지막 발급 판, 없으면 최신 판), 판에 없는 입력칸은 숨김. `preview.html`·`detail.html`·`list.html`: 판과 `check_mode` 표시, I8 판정 결과. `verify.html`: `license_ver`·`check_mode`·적용한 규칙 |
| 저장 | `LicenseRepository`·`IssueService`(감사 기록)가 상수 대신 계약의 판을 씀. `LicenseRecord`에 `checkMode` |
| DB(새 마이그레이션) | `license_ver` 제약을 `CHECK (license_ver = 1)`(V3)에서 아는 판 목록으로 바꿈. `check_mode` 열(NULL 허용) 추가, I3을 DB `CHECK`로도 고정(안 A: `(license_ver = 1 AND check_mode IS NULL) OR (license_ver >= 2 AND check_mode = 'minimal')`, 안 C: `check_mode IS NULL OR (license_ver >= 2 AND check_mode = 'minimal')`). `license_guard()`의 불변 비교 목록에 `check_mode`. V1~V6은 고치지 않음 |
| 배포 | 서버 이미지 빌드에서 SDK 최저 코어 `licensectl`을 태그로 빌드해 넣음(I8). 서버 패키지 버전 y를 올림(D44) |

### 13.5 검사 (발급 서버 검증 화면)
- 검증 화면은 서버의 최신 `licensectl`로 판정합니다. 최신 코어는 모든 판을 알므로, 판 1·판 2 파일 모두 자기 판의 전체 규칙으로 봅니다.
- I8을 넣으면 같은 화면에 "SDK 최저 코어(3.0.0) 판정" 결과도 함께 보여 줍니다. 현장 SDK가 실제로 받는지는 이 결과가 근거입니다.

### 13.6 미결
**지금 확인이 필요한 것(2단계 구현 전):**
0. §13.8의 D1~D4. D46 ②를 구체화하는 것이므로, 정하면 새 결정 번호로 기록합니다.

**판 2를 만들 때 결정할 것:**
1. 사이트별 SDK 코어 기록 방식: 발급 화면에서 사람이 판을 고르기(기본값 규칙 §13.4) / 사이트 장부(`site_id` → SDK 코어 버전)를 따로 둠
2. I8에서 판정할 코어 목록: 최저 코어만 / SDK에 나간 모든 코어
3. 판 2의 첫 내용(어떤 비추가형 변경인지). 안정 필드를 바꾸면 `minimal`로 전방 호환이 되지 않습니다

### 13.7 단계별 순서
| 단계 | 내용 | 상태 |
|---|---|---|
| 1 | `signature` 모드 삭제(코어 3.0.0) | **완료**(2026-10-21, `73e9f13`) |
| 2 | `check_mode` 판정(§9, R13 포함), `licensectl verify`·`InfoToJson` 출력, 발급 서버 검증 화면에 `license_ver`·`check_mode` 표시, I1·I3을 지키는지 확인하는 시험(발급 파일에 `issued_at`이 있고 DB 값과 같음, `check_mode` 없음), `sdk-v3.0.0` 발행, 발급 서버 y 버전 발행(새 `licensectl` 포함) | SDK 첫 적용 전 |
| 3 | 첫 추가형 새 필드: A1~A10, §6.2 변경 목록, C4(SDK 최저 코어 시험), I8 | 필요해질 때 |
| 4 | 첫 비추가형 변경(판 2): §13.4 전체, I3~I7, 하위 버전 발급 | 필요해질 때. §13.6 결정 후 |

### 13.8 결정 자료: R13·R17·I3 (2026-10-21)

**`check_mode`가 실제로 하는 일.** R13을 넣으면 `check_mode`는 "**이 판을 모르는 검증기에게 주는 지시**"가 됩니다.

| 파일 | 그 판을 아는 코어 | 그 판을 모르는 코어 |
|---|---|---|
| 키 없음(= `full`) | 그 판 전체 규칙 | `1006` 거부(실패 폐쇄) |
| `"minimal"` | 그 판 전체 규칙(키 없음과 **같음**) | 안정 필드 + 기간만 보고 받음 |

**"아는 판"이란.** 코어에 컴파일된 판 목록입니다. 3.0.0은 `detail.h`의 `kLicenseVer = 1` 하나뿐이고, SDK를 빌드할 때 고정됩니다. 라이선스 파일이나 실행 중 설정으로는 바뀌지 않습니다. 지금은 이 값을 밖에서 물어볼 API가 없습니다(R23).

| # | 결정 | 안 | 권고 |
|---|---|---|---|
| D1 (R13) | 아는 판인데 `minimal`인 파일을 어떻게 판정하나 | ① 그 판 전체 규칙 ② 항상 최소 판정 | **①**. ②면 판 2를 아는 새 SDK도 판 2 `minimal` 파일을 최소 판정만 하므로, 같은 SDK의 집행 강도가 발급자의 선택에 따라 달라짐. ①이면 새 SDK는 언제나 그 판 전체 규칙 |
| D2 (R17) | `fields.check_mode`에 무엇을 담나 | ① 적용한 수준 ② 파일에 적힌 값 | **①**. 로그에 `Minimal`이 보이면 "이 SDK는 이 판을 모름 = SDK 업그레이드 대상"이라는 뜻이 됨. 파일 값은 `licensectl verify`로 언제든 볼 수 있음 |
| D3 (I3) | 판 2 이상에 `minimal`을 언제 넣나 | 안 A: 항상 / 안 C: 사이트 기록상 모든 SDK가 그 판을 알면 키 없음, 아니면(모름·혼재·업그레이드 예정) `minimal` | §13.8 아래 비교 |
| D4 (R23) | 3.0.0에 라이브러리 정보 조회 API를 넣나 | 예: `LibraryVersion()`(예 `"3.0.0"`), `MaxLicenseVer()`(예 1)를 SDK가 `connect` 때 로그에 남김 / 아니오 | **예**. 사이트 SDK가 아는 판을 현장 로그로 확인할 수 있는 유일한 길. 첫 적용 코어에만 넣을 수 있음(나중에 넣으면 이미 나간 SDK는 알려 주지 못함) |

**D3 비교.**
| | 안 A(판 2 이상은 항상 `minimal`) | 안 C(기록 기반) |
|---|---|---|
| 사이트 SDK가 판을 알 때 | 키 없음과 결과 같음(D1 ①) | 키 없음 |
| 발급자가 사이트 SDK를 잘못 알았을 때 | 받음. 새 필드는 조용히 미집행(A8), 표시 필드 빈 값 | 거부 `1006`. JNI `connect` 방식은 STT 서버가 뜨지 않음, Manager는 기존 파일 유지 후 그 파일의 만료 때 멈춤 |
| SDK 업그레이드 전에 미리 발급 | 업그레이드 전·후 모두 받음, 업그레이드 뒤에는 자동으로 전체 규칙 | `minimal`로 발급하면 같음(기록이 "업그레이드 예정") |
| 필요한 것 | 없음 | 사이트별 SDK 코어 기록(§13.6 미결 1), 가능하면 D4 |
| 성격 | 가용성 우선 | 실패 폐쇄 우선 |

- 판 1만 있는 동안(지금, 2단계까지)은 D3과 무관합니다. 판 1은 언제나 키 없음입니다.
- 사용자 질문 "이전보다 높은 판이 아니면 `full`로 다시 발행해도 되나": **된다.** 다만 기준은 "그 사이트의 이전 라이선스 판"이 아니라 "그 사이트 SDK가 아는 판"입니다. 이전 라이선스가 키 없음(`full`)으로 받아들여졌다면 그 판은 안다는 증거가 됩니다. 이전 라이선스가 `minimal`이었다면 아무 증거가 되지 않습니다(모르는 SDK도 받으므로).

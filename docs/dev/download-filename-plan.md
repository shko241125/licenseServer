# 다운로드 파일 이름의 한글 사이트 ID: 분석과 구현 계획 (발급 서버)

- **상태:** **결정: 안 C(지금 규칙 유지, 문서만 보강), 2026-10-27 사용자, D48.** 코드는 바꾸지 않습니다. 안 A의 설계(§3)는 나중에 다시 검토할 때를 위해 남겨 둡니다. 결정 전 기록은 아래 그대로입니다.
- **보고(사용자, 2026-10-27):** 사이트를 한국어로 입력해 발급한 뒤 라이선스 파일을 내려받으니, 파일 이름이 깨진 것처럼(문자 인코딩 문제처럼) 보였다.
- **결론:** 문자 인코딩 문제가 아닙니다. 서버가 파일 이름을 만들 때 사이트 ID에서 영문·숫자·`_`·`-`가 아닌 문자를 **모두 `_`로 바꾸는 규칙** 때문입니다. 한글 사이트 ID는 전부 `_`가 되어, 이름만 보고는 어느 사이트 파일인지 알 수 없습니다. 라이선스 **내용**(서명된 `site_id`)은 정상입니다.

## 1. 재현과 원인

### 1.1 코드 경로
| 단계 | 위치 | 동작 |
|---|---|---|
| 이름 만들기 | `license-server/src/main/java/com/personaai/license/server/license/LicenseRecord.java` `downloadFileName()` | `siteId.replaceAll("[^A-Za-z0-9_-]", "_") + "_" + licenseId + ".lic"` |
| 응답 헤더 | `license-server/src/main/java/com/personaai/license/server/web/LicenseController.java` `download()` | `ContentDisposition.attachment().filename(이름, UTF_8)` |
| 시험 | `license-server/src/test/java/com/personaai/license/server/web/WebSecurityIT.java` | 영문 사이트(`SITE-A`)만 확인. 한글 사이트 시험 없음 |
| 문서 | `license-server/docs/user-manual.md` §5(발급 뒤 전달) | "영문·숫자·`_`·`-` 밖의 문자는 `_`로 바뀜"이라고 이미 적혀 있음 |

- 이 규칙은 첫 구현(`eeabf0a`, 2026-09-29)부터 있었습니다. 이를 정한 결정(DECISIONS) 항목은 없고, 주석 "파일 이름에 쓸 수 없는 문자는 '_' 로"가 근거의 전부입니다.
- 한글은 파일 이름에 쓸 수 있는 문자이므로, 이 규칙은 필요 이상으로 넓습니다.

### 1.2 실측 (2026-10-27, 서버가 쓰는 spring-web 7.0.9의 `ContentDisposition`)
| 사이트 ID | 지금 헤더(`Content-Disposition`) | 브라우저가 저장하는 이름 |
|---|---|---|
| `SITE-A` | `attachment; filename="SITE-A_<id>.lic"; filename*=UTF-8''SITE-A_<id>.lic` | `SITE-A_<id>.lic` |
| `서울센터` | `attachment; filename="_____<id>.lic"; filename*=UTF-8''_____<id>.lic` | `_____<id>.lic` (한글 4자 → `_` 4개 + 구분자 `_`) |
| `부산 지점(2)` | `…filename*=UTF-8''______2__<id>.lic` | `______2__<id>.lic` |

- 헤더 자체는 정상입니다. Spring이 이미 RFC 6266 형식으로 ASCII 대체 이름(`filename=`)과 UTF-8 이름(`filename*=`)을 둘 다 보냅니다. 서버가 UTF-8 이름을 주기 **전에** 한글을 지워 버리는 것이 문제입니다.
- 같은 Spring에 한글 이름을 그대로 넘기면 `filename*=UTF-8''%EC%84%9C%EC%9A%B8%EC%84%BC%ED%84%B0_<id>.lic`가 나옵니다. 이것을 다시 읽으면(`ContentDisposition.parse`) `서울센터_x.lic`로 되돌아옵니다(실측).
  - 이때 ASCII 대체 이름에서는 비ASCII 문자가 `_`로 바뀝니다(UTF-8 바이트마다 하나씩). 이 대체 이름은 `filename*`를 모르는 오래된 클라이언트만 씁니다.
  - RFC 6266 §4.3은 두 매개변수가 함께 있으면 `filename*`를 우선하도록 정합니다. 현행 주요 브라우저(Chrome·Edge·Firefox·Safari)는 이를 따릅니다.
- **"인코딩이 깨졌다"로 보인 이유(추정):** 이름이 `_____…`처럼 밑줄로만 이어져 글자가 사라진 것처럼 보입니다.
  - 만약 `ì„œìš¸`처럼 다른 글자로 보였다면 지금 코드로는 나올 수 없는 결과입니다(서버가 ASCII만 보냄).
  - 그 경우는 압축 프로그램(Windows의 CP949 zip을 리눅스에서 풂)이나 전달 경로를 확인해야 합니다. 화면에 보인 이름을 알려 주시면 확정하겠습니다.

## 2. 고칠 범위

| 대상 | 바꾸나 | 이유 |
|---|---|---|
| `LicenseRecord.downloadFileName()` | **예** | 원인 |
| `LicenseController.download()` | 아니오 | Spring 7.0.9가 RFC 6266 헤더(`filename` + `filename*`)를 이미 만듦 |
| 서명된 `site_id`, DB, 발급 입력 | **아니오** | 파일 이름만 바꿉니다. 라이선스 내용·서명·장부는 그대로입니다 |
| `licensectl issue -ledger`의 사본 이름(`SafeName`, `tools/licensectl/main.cpp`) | 아니오(범위 밖) | 발급 서버는 `-ledger`를 쓰지 않습니다. CLI 단독 운영용 사본 이름이고 코어 SDK 패키지에도 들어가므로, 바꾸려면 따로 다룹니다 |
| 공개키 다운로드(`public.key`) | 아니오 | 고정 ASCII 이름 |

## 3. 설계 (권장안 A)

### 3.1 규칙
`<사이트 부분>_<라이선스 ID>.lic`의 형태는 그대로 두고, **사이트 부분**만 다음 순서로 만듭니다.

1. **NFC 정규화.** 한글이 자모로 분해된(NFD) 입력이면, Windows 탐색기에서 `ㅅㅓㅇㅜㄹ`처럼 풀어져 보입니다. 이름에만 적용하고 서명된 `site_id`는 건드리지 않습니다.
2. **코드 포인트 단위로 판정.** `Character.isLetterOrDigit(cp)`이거나 `-`·`_`이면 그대로 두고, 나머지는 `_` 하나로 바꿉니다.
   - 한글·한자·라틴 문자 등 모든 문자 체계의 글자와 숫자가 남습니다.
   - 아래 문자는 모두 글자·숫자가 아니라서 `_`가 됩니다.
     - 공백, `.`, Windows 예약 문자 `< > : " / \ | ? *`, 제어문자
     - 방향 제어 문자(U+202E 등): 확장자를 거꾸로 보이게 해 속이는 데 쓰입니다
     - 이모지
   - 지금 코드는 `char`(UTF-16) 단위라서 이모지 하나가 `_` 두 개가 됩니다. 코드 포인트 단위로 바꾸면 하나가 됩니다.
3. **길이 제한 50 코드 포인트.** 뒤를 자릅니다(코드 포인트 경계라서 글자가 반으로 갈리지 않음).
   - 근거는 아래 계산과 같습니다. 사이트 ID 입력 한도는 256바이트(한글 약 85자)입니다.

   | 파일 시스템 | 이름 한도 | 이 규칙의 최대 이름 길이 |
   |---|---|---|
   | 리눅스 ext4·xfs | 255바이트 | 사이트 50자 × 최대 4바이트 = 200바이트, 여기에 `_` + ID 32자 + `.lic` = 37바이트 → 237바이트 |
   | Windows NTFS | 255 UTF-16 단위 | 237 이하 |

   - 이름이 잘려도 같은 이름이 생기지 않습니다. 이름에 라이선스 ID(고유)가 들어 있기 때문입니다.
4. 결과 예

   | 사이트 ID | 파일 이름 |
   |---|---|
   | `SITE-A` | `SITE-A_<id>.lic` (지금과 같음) |
   | `서울센터` | `서울센터_<id>.lic` |
   | `부산 지점(2)` | `부산_지점_2__<id>.lic` |
   | `site.v2` | `site_v2_<id>.lic` (지금과 같음) |
   | 이모지 포함 `센터😀` | `센터__<id>.lic` (이모지 → `_` 하나 + 구분자) |

- `.`가 이름 중간에 남지 않으므로, 끝 점·이중 확장자 문제와 Windows 예약 이름(`CON.lic` 등) 문제가 생기지 않습니다. 사이트 부분 뒤에 항상 `_<id>`가 붙습니다.

### 3.2 코드 (예상 diff 크기: 메서드 1개)
```java
/** 파일 이름: 사이트 ID 의 글자·숫자·'-'·'_' 만 남기고(나머지는 '_'), NFC, 50 코드 포인트까지. 서명된 site_id 는 그대로. */
public String downloadFileName() {
    StringBuilder b = new StringBuilder();
    Normalizer.normalize(siteId, Normalizer.Form.NFC).codePoints().limit(50)
            .forEach(c -> b.appendCodePoint(Character.isLetterOrDigit(c) || c == '-' || c == '_' ? c : '_'));
    return b + "_" + licenseId + ".lic";
}
```
- `LicenseController`는 바꾸지 않습니다.
- 이 코드를 따로 실행해 §3.1의 예 5개, NFD 입력(`서울` 5자 → NFC 2자), U+202E, 예약 문자 9개, `CON`을 확인했습니다(2026-10-27). 길이도 확인했습니다: 한글 85자는 187바이트, 4바이트 글자 85자는 237바이트로 모두 255바이트 이하입니다.

### 3.3 시험
| 시험 | 위치 | 확인 내용 |
|---|---|---|
| 단위(새 파일) | `license-server/src/test/java/com/personaai/license/server/license/LicenseRecordTest.java` | §3.1 예 5개. NFD로 넣은 `서울` → NFC `서울`. 방향 제어 문자 U+202E → `_`. Windows 예약 문자 9개 → `_`. 한글 85자 → 50자로 잘림, UTF-8 255바이트 이하. 4바이트 글자(예: U+20000) 85자 → 255바이트 이하 |
| 웹 통합 | `WebSecurityIT` 다운로드 시험에 한글 사이트 한 건 추가 | `Content-Disposition`에 `filename*=UTF-8''%EC%84%9C%EC%9A%B8…_<id>.lic`. 기존 `SITE-A` 단언은 그대로 |
| 수동 | 로컬 시험 스택(사용자 개발 스택 `license-dev` 아님) | Windows Chrome·Edge에서 내려받아 저장된 이름이 `서울센터_<id>.lic`인지 확인 |

실행: `(cd license-server && ./gradlew test)`, `./gradlew integrationTest`. 필요하면 `scripts/make-server-release.sh --worktree --verify`.

### 3.4 문서와 버전
- `license-server/docs/user-manual.md` §5: 파일 이름 규칙을 새 규칙으로 바꿈(한글 유지, 그 밖의 기호는 `_`, 50자).
- `docs/dev/DECISIONS.md`: 새 결정 D48(다운로드 파일 이름 규칙과 이유).
- `docs/dev/STATUS.md`: 구성 요소 표와 검증 기록.
- 버전: 동작 수정이고 DB·형식 변경이 없으므로 **서버 1.8.1**(D44의 버전 원본 `license-server/build.gradle.kts`), 발행은 요청이 있을 때.
- 이미 내려받은 파일은 그대로 유효합니다. 이름만 다를 뿐 내용과 서명은 같습니다.

## 4. 결정할 것 (사용자)

| 안 | 내용 | 장점 | 단점 |
|---|---|---|---|
| **A (권장)** | §3: 한글 등 모든 글자 유지, 기호만 `_`, NFC, 50자 | 이름으로 사이트를 알아봄. 코드 한 메서드 | 한글 이름을 폐쇄망 리눅스 서버에서 다루는 불편이 있을 수 있음(아래) |
| B | ASCII 유지. 사이트 부분을 빼고 `license_<id>.lic` | 어디서나 안전 | 이름으로 사이트를 알 수 없음(지금과 비슷) |
| C | 지금 규칙 유지, 문서만 보강 | 변경 없음 | 보고된 불편이 그대로 남음 |

**안 A의 운영 주의(매뉴얼에 적을 내용):**
- 현장 STT 서버로 옮길 때는 받는 쪽이 정한 이름(보통 `license.lic`)으로 바꾸는 것이 지금도 권장 사항입니다(사용자 매뉴얼 §5). 이름을 바꿔도 서명에는 영향이 없습니다.
- Windows에서 한글 파일 이름을 zip으로 묶어 리눅스에서 풀면, 압축 프로그램에 따라 이름이 깨질 수 있습니다(zip의 CP949 이름). 이때는 이름을 바꾸거나 묶지 않고 옮깁니다. 내용은 영향을 받지 않습니다.

## 5. 진행 순서 (안 A 승인 시. 2026-10-27 안 C로 결정되어 진행하지 않음)
1. 안 결정 → DECISIONS D48
2. `LicenseRecord.downloadFileName()` 수정 + `LicenseRecordTest` 추가 + `WebSecurityIT` 한글 건 추가
3. `./gradlew test`·`integrationTest` 실행, 결과 기록
4. 사용자 매뉴얼 §5·STATUS 갱신, 버전 1.8.1
5. 커밋(주제별: `fix(server)` 코드+시험, `docs` 문서), 요청 시 푸시·발행

## 6. 결정과 반영 (2026-10-27)
- **결정:** 안 C. 다운로드 파일 이름 규칙(`[^A-Za-z0-9_-]` → `_`)을 유지하고 문서만 보강합니다(D48).
- **이유(사용자):** 규칙을 단순하게 둡니다. 파일 이름이 ASCII뿐이면 폐쇄망 리눅스 서버, 압축(CP949 zip), 셸 입력에서 이름이 달라질 일이 없습니다. 사이트는 이름 뒤의 라이선스 ID로 구분됩니다.
- **바꾸지 않는 것:** 라이선스 **내용**에는 한글을 그대로 씁니다(`site_id`·`project_name` 등). 파일 내용은 UTF-8이고, SDK는 `Utf16ToUtf8`/`JStringToUtf8`로 읽으며, 이모지 수용 시험까지 통과합니다. 내용의 한글을 막으면 입력 검증(`ContractValidator`)과 이미 발급한 건의 이력 검색이 함께 바뀌고, 단순화로 얻는 것이 없습니다.
- **반영한 문서:** `license-server/docs/user-manual.md` §5.3(사이트 ID 칸), §5.5(이름 규칙·예·정상 안내·라이선스 ID로 구분), §10(문제 해결 행), §12(FAQ).
- 코드 주석 "파일 이름에 쓸 수 없는 문자는 '_' 로"(`LicenseRecord.downloadFileName()`)는 실제 이유(ASCII로 고정)와 다르지만, 이번에는 문서만 고친다는 결정에 따라 그대로 둡니다. 다음에 이 파일을 고칠 때 함께 바로잡습니다.

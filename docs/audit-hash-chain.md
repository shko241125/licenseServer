# 감사 로그 해시 체인 — 무결성 검사 방법론

사내 라이선스 발급 서버(`license-server/`)의 감사 로그가 **어떻게 기록되고, 어떻게 검사하며, 무엇을 탐지하고 무엇을 탐지하지 못하는지** 정리한 문서입니다.

| 구성 요소 | 위치 |
|---|---|
| 기록·검사 코드 | `license-server/src/main/java/com/personaai/license/server/audit/AuditService.java` |
| 테이블·권한 | `license-server/src/main/resources/db/migration/V1__init.sql` (`audit_event`) |
| 앵커 기록 | `license-server/deploy/backup.sh` |
| 앵커 대조 | `license-server/deploy/verify-anchors.sh` |
| 테스트 | `DatabaseGuardIT.hashChainDetectsTampering`, `DatabaseGuardIT.appAccountCannotRewriteHistory` |

---

## 1. 핵심 아이디어

각 로그 행에 **바로 앞 행의 해시**를 넣고, 그것까지 포함해 자기 해시를 계산합니다.

```
행 1: prev=000…0(GENESIS) ─┐
      내용1                  ├─ hash1 = SHA256(000…0 + 내용1)
행 2: prev=hash1 ───────────┤
      내용2                  ├─ hash2 = SHA256(hash1 + 내용2)
행 3: prev=hash2 ───────────┤
      내용3                  └─ hash3 = SHA256(hash2 + 내용3)
```

어느 한 행을 고치면 그 행의 해시가 바뀌고, 다음 행에 적힌 `prev_hash`와 맞지 않게 됩니다. 사슬의 고리 하나를 바꾸면 이어진 고리와 맞물리지 않는 것과 같습니다.

> 비유가 깨지는 지점: 사슬 **전체를 새로 만들어 끼우는** 공격은 이 구조만으로는 막지 못합니다(§4). 그래서 체인 밖에 적어 두는 **앵커**가 필요합니다(§5).

---

## 2. 기록 방법 (`AuditService.record`)

### 2.1 해시 계산식
```
hash = SHA-256( prev_hash + "\n" + JSON([seq, at, actor, action, target, client_ip, detail_json]) )
```

| 필드 | 값 | 재현성을 위한 처리 |
|---|---|---|
| `prev_hash` | 직전 행의 `hash`. 첫 행은 `0` 64개(`GENESIS`) | — |
| `seq` | DB 시퀀스 번호 | 해시에 포함 → 순서를 바꾸면 탐지 |
| `at` | 기록 시각 | **마이크로초로 잘라** `yyyy-MM-ddTHH:mm:ss.SSSSSSZ`로 표기. PostgreSQL `TIMESTAMPTZ` 저장 정밀도와 같아서, 다시 읽어도 같은 문자열이 나옴 |
| `actor`, `action`, `target`, `client_ip` | 누가, 무엇을, 무엇에, 어디서 | `null`은 JSON `null`로 고정 |
| `detail_json` | 상세 정보 | **키를 정렬한 JSON 문자열**을 만들고, **그 바이트 그대로** TEXT 열에 저장 |

설계 이유:
- **필드를 JSON 배열로 묶는 이유:** 문자열을 그냥 이어 붙이면 `actor="ab", action="c"`와 `actor="a", action="bc"`가 같은 입력이 됩니다. 배열로 직렬화하면 필드 경계가 따옴표와 쉼표로 고정됩니다.
- **`detail`을 JSONB가 아니라 TEXT로 저장하는 이유:** JSONB는 저장할 때 키 순서와 공백을 정규화해서, 해시를 계산한 원래 바이트를 되살릴 수 없습니다. 그러면 검사 때 같은 해시를 다시 만들 수 없습니다.

### 2.2 기록 절차 (한 트랜잭션)
```
1. pg_advisory_xact_lock(LOCK_KEY)          ← 기록을 한 줄로 세움
2. SELECT hash … ORDER BY seq DESC LIMIT 1  ← 직전 해시 (없으면 GENESIS)
3. nextval('audit_event_seq')               ← 이번 seq
4. at = now (µs), detail 정규화, hash 계산
5. INSERT
6. COMMIT → 잠금 자동 해제
```

| 결정 | 이유 |
|---|---|
| 잠금 | 두 요청이 동시에 2번을 실행하면 같은 직전 해시를 읽어 **체인이 갈라집니다.** "읽고 → 쓰기"를 한 번에 하나씩만 하게 합니다 |
| `FOR UPDATE`가 아니라 **advisory lock** | 앱 계정은 감사 테이블에 INSERT·SELECT만 할 수 있는데, PostgreSQL의 `SELECT … FOR UPDATE`는 UPDATE 권한이 필요합니다. advisory lock은 테이블 권한과 무관한 이름 붙은 잠금입니다 |
| 별도 트랜잭션(`REQUIRES_NEW`) | 발급이 실패해 원래 트랜잭션이 롤백돼도 `ISSUE_FAILED` 같은 기록은 남습니다 |
| seq에 빈 번호 허용 | `nextval` 뒤에 롤백되면 번호가 빕니다. 연결은 `prev_hash`로 확인하므로 체인에는 영향이 없습니다. 대신 **빈 번호만 보고 삭제 여부를 판단할 수는 없습니다**(§4) |

### 2.3 기록되는 행위
로그인 성공·실패, OTP 실패, 비밀번호·OTP 변경, 계정 관리, 키 해제·봉인(성공·실패·잠금), 발급(성공·실패), 다운로드, 무효 처리, 검증 요청, 무결성 검사.

---

## 3. 검사 방법 (`AuditService.verifyChain`)

```
prev = GENESIS
for 각 행 (seq 오름차순):
    ① 연결 검사: row.prev_hash == prev ?            아니면 → "prev_hash does not link to previous row"
    ② 내용 검사: 저장된 필드로 hash 재계산 == row.hash ?  아니면 → "row content does not match its hash"
    prev = row.hash
결과: 정상 여부, 검사 건수, 처음 끊긴 seq, 사유, 마지막 해시
```

- ①은 **행 사이의 연결**(삭제·끼워 넣기·순서 변경)을, ②는 **행 자체의 내용**(필드 수정)을 봅니다.
- 처음부터 다시 계산하므로 비용은 O(행 수)입니다.
- 실행 위치: 화면 **감사 로그 → "해시 체인 무결성 검사"**(ADMIN). 검사 행위도 `AUDIT_VERIFY`로 기록되고, 결과로 나온 마지막 해시도 함께 남습니다.

---

## 4. 무엇을 탐지하나 — 조작 유형별

| 조작 | 결과 | 검사에서 드러나는 방식 | 근거 |
|---|---|---|---|
| 한 행의 내용 수정(예: `detail_json`) | **탐지** | 그 행에서 ② 실패 | 통합 테스트 |
| 중간 행 삭제 | **탐지** | 다음 행에서 ① 실패 | 통합 테스트 |
| 중간에 가짜 행 끼워 넣기 | **탐지** | 가짜 행이나 다음 행에서 ① 실패 | 알고리즘 |
| 두 행 순서 바꾸기, seq 변경 | **탐지** | ① 또는 ② 실패(seq가 해시에 포함됨) | 알고리즘 |
| **마지막 N행 삭제**(꼬리 자르기) | 체인 검사로는 **탐지 못 함** | 남은 체인은 그대로 온전함 | 알고리즘상 한계 → §5 |
| **수정한 행부터 끝까지 해시 전부 재계산**, DB 통째 교체 | 체인 검사로는 **탐지 못 함** | SHA-256에는 비밀키가 없어서 쓰기 권한이 있으면 누구나 다시 계산 가능 | 알고리즘상 한계 → §5 |

그리고 이 표의 조작은 애초에 **앱 계정으로는 할 수 없습니다.** 앱 계정(`license_app`)에는 감사 테이블 UPDATE·DELETE·TRUNCATE 권한이 없고, 통합 테스트로 확인했습니다. 따라서 방어가 두 겹입니다.
1. **DB 권한:** 웹 서버가 뚫려도 앱 계정으로는 기록을 고칠 수 없습니다.
2. **해시 체인과 앵커:** DB 소유자·슈퍼유저 권한으로 고쳐도 흔적이 남습니다.

---

## 5. 한계를 메우는 앵커 (체인 밖의 기록)

체인이 **자기 자신만으로는** 증명하지 못하는 것은 "원래 어디까지 있었는가", "원래 해시가 무엇이었는가"입니다. 그래서 백업할 때마다 그 시점의 마지막 `seq`와 `hash`를 **DB 밖 파일**에 누적 기록합니다.

```
backups/audit-anchor.log            (backup.sh 가 한 줄씩 추가)
20260929T073838Z 11 3116bff0…110e 8a50ce67…daa4
<백업 시각 UTC>  <seq> <그 행의 hash> <덤프 파일 sha256>
```

### 5.1 대조 방법
각 앵커 줄에 대해 DB의 같은 `seq` 행이 **있고**, 그 `hash`가 **같아야** 합니다.
```bash
cd <배포 디렉터리>
./verify-anchors.sh                   # 기본: ./backups/audit-anchor.log
./verify-anchors.sh /mnt/usb/audit-anchor.log   # 외부 보관본으로 대조 (권장)
```
출력 예:
```
FAIL  seq 11  (20260929T073838Z)  anchor=3116bff0…  db=3a64cf52…
OK    seq 19  (20260929T085443Z)
MISMATCH: 감사 로그가 앵커 기록 이후 삭제·교체되었을 수 있음        (종료 코드 1)
```
위 예시는 개발 환경에서 실제로 나온 결과입니다. seq 11의 앵커를 기록한 뒤 DB를 초기화(통째 교체)했더니, 대조에서 불일치로 드러났습니다.

| 조작 | 앵커 대조 결과 |
|---|---|
| 꼬리 자르기 | 앵커의 seq 행이 **없음** → `db=<행 없음>` |
| 체인 전체 재계산 / DB 바꿔치기 | 앵커 해시와 DB 해시가 **다름** |

### 5.2 앵커 보관 조건
- 앵커 파일을 **공격자가 함께 고칠 수 없는 곳**에 두어야 의미가 있습니다. 예: 다른 서버, 오프라인 매체, 결재 문서에 마지막 해시 기재.
- 같은 호스트에만 있으면 DB와 함께 조작될 수 있습니다.

### 5.3 스크립트 작성 시 주의 (실제로 겪은 버그)
`while read … done < 파일` 반복문 안에서 `docker compose exec`를 부르면, 이 명령이 **반복문의 표준입력(앵커 파일)을 읽어 버립니다.** 그러면 첫 줄만 검사하고 끝납니다. `verify-anchors.sh`는 `< /dev/null`로 이 문제를 막아 두었습니다. 직접 스크립트를 쓸 때도 같은 처리를 해야 합니다.

---

## 6. 운영 절차

| 시점 | 할 일 |
|---|---|
| 매일 | `backup.sh`(덤프 + 앵커 추가). 앵커 파일은 **다른 곳에도 복사** |
| 주 1회 | 화면에서 해시 체인 무결성 검사 → "정상" 확인 |
| 월 1회, 실사 전 | `verify-anchors.sh`로 외부 보관본과 대조 |
| 복원 후 | 무결성 검사 결과의 마지막 해시가 복원한 백업의 앵커와 같은지 확인 |
| 이상 발견 시 | 처음 끊긴 `seq`와 사유를 기록하고 서버를 격리한 뒤, 가장 최근 정상 백업 덤프와 비교해 조작 범위를 파악 |

---

## 7. 한계와 개선안

현재 화면의 무결성 검사는 **체인 내부 일관성만** 봅니다. 꼬리 삭제와 체인 전체 재계산은 **앵커 대조(§5)를 해야** 드러납니다.

| 개선안 | 효과 | 비용 |
|---|---|---|
| A. 앵커 대조를 화면에서도 실행(앵커 파일 업로드) | 절차 누락 방지 | 작음 |
| B. 주기적 체크포인트에 **Ed25519 서명**(별도 키, 오프라인 보관) | 서명 키 없이는 체크포인트를 위조할 수 없음 → 전체 재계산 공격 차단 | 중간(키 관리 추가) |
| C. SHA-256 대신 **HMAC-SHA256**(키는 DB 밖 secret) | DB만 가진 공격자는 체인을 다시 계산할 수 없음 | 작음. 단 키가 같은 서버에 있으면 서버를 장악당하면 무력 |
| D. 기록 즉시 외부 로그 서버(syslog, WORM 저장소)로 사본 전송 | 가장 강함 | 인프라 필요. 현재 설계(외부 통신 차단)와 조정 필요 |

권장 순서는 **A → B**입니다.

---

## 8. 용어

| 용어 | 뜻 |
|---|---|
| 해시 체인 | 각 항목이 이전 항목의 해시를 포함하는 추가 전용 기록 구조 |
| GENESIS | 첫 행의 `prev_hash`로 쓰는 고정값(`0`×64) |
| 앵커 | 특정 시점의 (seq, hash)를 체인 밖에 적어 둔 기록. 체인 자체로는 증명할 수 없는 "원래 끝"과 "원래 값"을 증명 |
| advisory lock | PostgreSQL에서 테이블이나 행이 아니라 임의의 숫자에 거는 잠금. `pg_advisory_xact_lock`은 트랜잭션이 끝나면 자동으로 풀림 |
| HMAC | 비밀키가 들어간 해시. 키 없이는 올바른 값을 만들 수 없음 |

참고: PostgreSQL 문서 "Explicit Locking – Advisory Locks", RFC 6962(Certificate Transparency — 머클 트리 기반 추가 전용 로그), Crosby & Wallach, "Efficient Data Structures for Tamper-Evident Logging"(USENIX Security 2009).

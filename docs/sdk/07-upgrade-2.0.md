# 07. 1.x → 2.0.0 업그레이드 안내

2.0.0은 **라이선스 형식과 공개 API가 모두 바뀐 호환 깨짐 버전**입니다. 1.x를 쓰던 SDK·STT 서버는 이 문서 순서대로 옮깁니다. 처음 적용한다면 이 문서는 건너뛰고 [README](README.md)부터 봅니다.

## 1. 한눈에 보기

| 구분 | 1.x | 2.0.0 |
|---|---|---|
| 형식 버전 키 | `format_version: 1` | `license_ver: 1` |
| 채널 | `allowed_channels.online_stt`, `offline_stt`(정수 2개) | `allowed_channels.total` + 이름 자유 항목 `{"세부 항목": 상한}`(예: `offline.sonastt`) |
| 기간 | `not_after` + `grace_period_days`(유예) | `not_after`만. **유예 없음.** 운영은 영구(`9999-12-31T23:59:59Z`) |
| 새 필드 | — | `solution` |
| 상태 | `NotYetValid`, `Valid`, `Grace`, `Expired` | `NotYetValid`, `Valid`, `Expired` |
| 새 오류 | — | `1011 LICENSE_ENGINE_NOT_LICENSED` |
| 서로 다른 버전 사이 | — | 1.x 파일 → 2.0.0: `1006`. 2.0.0 파일 → 1.x: 아래 §5 |

규격 전체: [04-license-format.md](04-license-format.md).

## 2. 코드 변경 (공개 헤더 `stt_license/license.h`)

### 2.1 서명 전용 방식(`VerifySignature` / `Verify(SignatureOnly)`)을 쓰는 경우

API는 그대로입니다. **값을 읽는 경로만 바꿉니다.**

```cpp
// 1.x
license.GetInt("allowed_channels.offline_stt", &offline);
// 2.0.0 — 전체 상한과 세부 항목 상한을 따로 읽는다. 키가 없으면 그 세부 항목은 허용되지 않은 것(0)으로 본다.
std::int64_t total = 0, sonastt = 0;
license.GetInt("allowed_channels.total", &total);
license.GetInt("allowed_channels.offline.sonastt", &sonastt);
```
- `validity.grace_period_days`를 읽던 코드는 지웁니다(키가 없음).
- SDK가 어떤 항목 이름(예: `offline`)과 세부 항목 이름(예: `sonastt`)을 읽을지 정해 둡니다. 항목 이름은 라이선스마다 정할 수 있습니다.
- SDK의 별도 만료 장치가 **영구 값 `9999-12-31T23:59:59Z`**를 만료로 오판하지 않는지 확인합니다(UTC 64비트 epoch로 비교. 현지 시각 변환·32비트 `time_t` 금지, 04-license-format.md §4).

### 2.2 `LicenseFields` / `ParseStandardFields` / `StateAt`

| 1.x | 2.0.0 |
|---|---|
| `format_version` | `license_ver` |
| `online_stt`, `offline_stt` | `total_channels` + `channels`(`std::map<std::string, std::map<std::string, int>>`, 항목 → 세부 항목 → 상한) |
| `grace_period_days` | 삭제 |
| — | `solution` |
| `State::Grace` | 삭제. `switch`에서 `Grace` 분기를 지웁니다(남겨 두면 컴파일 오류) |

```cpp
// 2.0.0
int sonastt = 0;
auto g = fields.channels.find("offline");
if (g != fields.channels.end()) {
  auto it = g->second.find("sonastt");
  if (it != g->second.end()) sonastt = it->second;
}
```

### 2.3 `Manager` 방식(파일 경로 + 정책 내장)

| 1.x | 2.0.0 |
|---|---|
| `enum class Kind { Online, Offline }` | **삭제.** 항목 이름 문자열 |
| `Acquire(Kind::Offline, &ch)` | `Acquire("offline", "sonastt", &ch)` — 세부 항목 상한과 `total`을 함께 지킴 |
| `MaxChannels(Kind::Offline)` | `MaxChannels("offline", "sonastt")`, 전체는 `TotalChannels()` |
| `Info::online_max/offline_max/…_used`, `grace_until` | `Info::total_max`, `total_used`, `engines`(`ChannelUsage{group, engine, max, used}`) |
| `InfoToJson` | `channels: {total:{max,used}, <항목>:{<세부>:{max,used}}}`, `solution` 추가, `grace_until` 삭제 |

세션 풀 크기 예(참조 구현 `integration/harness/harness_jni.cpp`의 `connect`):
```cpp
const int pool = std::min<int>(configured, mgr->MaxChannels("offline", engine));
```
`Acquire`의 새 실패 코드:
- `ChannelLimit`(`1009`): 그 세부 항목 상한 **또는** `total`이 소진됨. 잠시 뒤 재시도
- `EngineNotLicensed`(`1011`): 라이선스에 그 항목·세부 항목이 없거나 상한 0. 재시도해도 같음(설정·라이선스 확인)

## 3. STT 서버(Java)

- 반환 코드 표에 `1011`을 추가합니다(03-stt-server.md §2). `SonaSttErrorCode`에 `LICENSE_ENGINE_NOT_LICENSED`를 추가합니다.
- `1006`의 문구에 "이전 형식(format_version) 라이선스는 재발급"을 포함합니다.
- 읽기·전달 방식(`readAllBytes` + UTF-8)과 Docker 설정은 바뀌지 않습니다.

## 4. 빌드

- CMake 연동·공개키 주입·심볼 은닉 방식은 1.1.2와 같습니다(01-build.md). **1.1.1은 쓰지 않습니다**(키 경로 회귀).
- 공개키는 바뀌지 않았습니다. 패키지를 교체한 뒤 내장 키 확인(01-build.md §6.5)만 다시 합니다.

## 5. 배포 순서 (중요)

1. **SDK를 2.0.0으로 바꾸고 배포합니다.**
2. 그 뒤에 새 형식(`license_ver` 1) 라이선스를 발급·반입합니다.
3. 현장의 이전 형식 라이선스는 갱신 발급(발급 담당)으로 새 형식으로 바꿉니다. 2.0.0 SDK는 이전 형식을 `full`·Manager에서 `1006`으로 거부합니다.

순서를 바꾸면(새 라이선스를 1.x SDK에 넣으면, 실측):
- `full`·Manager: `1005`(`format_version` 없음)로 거부
- **`signature` 모드(기본): 통과**하지만 `allowed_channels.offline_stt`가 없어 채널 한도가 1.x SDK의 "값 없음" 기본 동작에 달림

## 6. 확인 목록

- [ ] `Kind`, `State::Grace`, `online_stt`/`offline_stt`/`grace_period_days`/`format_version` 참조가 코드에 남아 있지 않다
- [ ] 값 경로를 `allowed_channels.total`, `allowed_channels.<항목>.<세부 항목>`으로 바꿨다
- [ ] `1011` 처리와 로그 이름을 추가했다
- [ ] 영구 라이선스(9999-12-31)로 기동해 만료로 오판하지 않는다
- [ ] 이전 형식 라이선스로 기동하면 `full`에서 `1006`이다
- [ ] 05-testing.md §3 수용 기준을 2.0.0 기준으로 다시 통과했다

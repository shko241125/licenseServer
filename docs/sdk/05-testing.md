# 05. 테스트와 수용 기준

## 1. 개발용 키와 라이선스 만들기

패키지의 `licensectl`로 **개발 전용** 키쌍과 라이선스를 만듭니다. 운영 비밀키는 발급 서버에만 있습니다. SDK 개발자는 운영 비밀키를 받지 않습니다.

```bash
cmake -S . -B build -G Ninja && cmake --build build --target licensectl
./build/licensectl keygen -out build/dev-keys            # public.key / private.key (이미 있으면 실패)
./build/licensectl issue -key build/dev-keys/private.key \
    -in core/testdata/contract.example.json -out build/dev.lic
./build/licensectl verify -pub build/dev-keys/public.key build/dev.lic          # 종료코드 0 = VALID/GRACE
./build/licensectl verify -pub build/dev-keys/public.key -json - < build/dev.lic   # JSON 출력
```
- 계약 JSON(`contract.example.json`)에서 기간·채널을 바꿔 시험용 라이선스를 만듭니다. `format_version`, `license_id`, `issued_at`은 생략하면 자동으로 채워집니다.
- 개발 키로 빌드한 SDK는 **운영 라이선스를 거부하고, 개발 라이선스를 받아들입니다.** 이 빌드를 배포하지 않습니다(01-build.md §6).
- 개발 키와 라이선스는 `build/` 아래에 둡니다(git 무시). 저장소 루트의 `private.key`, `/keys/`, `/*.lic`도 무시되지만, 그것에 기대지 않습니다.

## 2. 패키지 전체 검증 `scripts/check.sh`

```bash
scripts/check.sh            # 결과물 ./build-check, 마지막 줄 "ALL CHECKS PASSED"
```
필요한 것: CMake·Ninja, GCC/Clang, OpenSSL 개발 패키지(`libssl-dev`, 정적 `libcrypto.a` 포함), GoogleTest, JDK 17 이상, Python 3, `pkg-config`.
- 헤드리스 JDK(`openjdk-*-jdk-headless`)로 충분합니다(AWT 불필요).
- CMake 3.24 미만에서 `Could NOT find JNI`가 나오면 `JAVA_HOME`을 지정합니다(예: `/usr/lib/jvm/java-17-openjdk-amd64`). 구버전 CMake 모듈은 새 JDK 경로를 모릅니다.
- 원 저장소에서는 `scripts/cloud-setup.sh`가 Ubuntu에 필요한 도구를 설치합니다.

| 단계 | 확인하는 것 |
|---|---|
| 1 | 코어 단위 테스트(GoogleTest)를 ASan+UBSan으로 실행 |
| 2 | 골든 벡터를 JDK 표준 Ed25519와 Python json으로 교차 검증 (정규화 호환성) |
| 3 | `licensectl` 전 명령 E2E, 키 혼동 거부, 원장 기록 |
| 4 | JNI 하네스를 정적 OpenSSL + 심볼 은닉으로 빌드. `Java_*` 외 export 0개 |
| 5 | Java ↔ JNI 결합 테스트: `connect(callback, configFile, hostLicense)` 모드별, 이모지 라이선스, 변조·만료, NUL·제어문자 보존 |
| 6 | `LD_PRELOAD`로 `EVP_DigestVerify`를 가로채는 공격. 보호 빌드는 막고, 대조군 2개(동적 libcrypto, 정적 libcrypto + 심볼 export)는 뚫려야 함 |
| 7 | SDK 형태 CMake 소비자: 부모 C++14/20, OpenSSL 재사용·사용자 지정 타깃, 키 누락·비밀키 오지정 거부. 공개 헤더 C++11~20 |

WSL에서는 리눅스 파일시스템(예: `~/work`)에 두고 실행합니다. `/mnt/c`에서는 권한 검사 일부를 건너뛰고 경고로 대신합니다.

## 3. SDK 적용 후 수용 기준 (SDK 저장소에서 확인)

실제 `libsonastt_jni_v2.so`와 STT 서버로 확인합니다. **전 항목 통과 전에는 배포하지 않습니다.**

**빌드 산출물**
- [ ] 심볼: `nm -D --defined-only libsonastt_jni_v2.so | awk '{print $3}' | grep -v '^Java_'` 출력 없음(또는 SDK가 의도한 공개 C API만)
- [ ] 정적 암호: `ldd libsonastt_jni_v2.so | grep libcrypto` 출력 없음(SDK가 정적 OpenSSL을 쓰는 경우)
- [ ] glibc: `objdump -T libsonastt_jni_v2.so | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1` 가 `GLIBC_2.35` 이하
- [ ] 공개키: 빌드에 쓴 키 파일의 지문이 발급 담당이 알려 준 **운영 지문**과 같음, 그리고 `.so`에 운영 키 문자열이 1회·개발 키는 0회(01-build.md §6.5)
- [ ] 운영 빌드가 `SDK_LICENSE_DEV_KEY=OFF`, 새 빌드 디렉터리에서 만들어짐(01-build.md §6.3)

**동작** (개발 키 빌드 + 개발 라이선스로)

| 입력 | `signature` | `full` |
|---|---|---|
| 정상 라이선스 | 0 | 0 |
| 채널 수 한 글자 변조 | 1004 | 1004 |
| 만료 라이선스 | 0 (SDK 만료 장치가 동작하는지 별도 확인) | 1008 |
| 시작 전 라이선스 | 0 | 1007 |
| 다른 키로 서명 | 1004 | 1004 |
| `project_name`에 이모지 포함 | 0 | 0 ← `GetStringUTFChars`를 쓰면 1003 |
| 빈 문자열 / JSON 아님 | 1003 | 1003 |
| 기존 `sonaLicense.*.key` | 공존 설정 on: 기존 경로 성공 / off: 1003 | 동일 |
| `--license-verify-mode=FULL`(대문자) | 연결 거부 | — |

**공격 저항**
- [ ] `LD_PRELOAD`로 `EVP_DigestVerify`를 항상 성공시키는 라이브러리를 주입해도 변조 파일이 `1004` (방법: `scripts/check.sh` 6단계의 `fake_verify.c`)

**값 사용**
- [ ] `offline_stt` = N일 때, 두 엔진 동시 세션 합계가 N을 넘지 않음(SDK 정책대로 적용한 경우)

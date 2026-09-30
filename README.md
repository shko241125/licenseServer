# licenseServer

폐쇄망 STT 제품용 오프라인 라이선스. 사내에서 Ed25519로 서명한 평문 `license.lic`를 한 번 반입하면, STT SDK가 내장 공개키로 스스로 검증하고 채널 수를 제한한다.

| 경로 | 내용 |
|---|---|
| `core/` | `stt_license` C++17 정적 라이브러리. 엄격 JSON 파서, RFC 8785 정규화, 서명 검증, 기간 판정, 채널 한도 |
| `tools/licensectl/` | 사내 발급 CLI (`keygen`, `issue`, `verify`, `sign`, `pubkey`) |
| `cmake/SttLicenseHardening.cmake` | `stt_license_harden_shared_library()` — JNI `.so`의 심볼 은닉 |
| `integration/harness/` | SDK 호출 모델을 흉내 낸 JNI 하네스 + Java 결합 테스트 |
| `integration/cmake-consumer/` | SDK CMake 빌드를 흉내 낸 소비자 프로젝트(연동 인수 테스트) |
| `license-server/` | **사내 발급 서버**(Spring Boot, Docker). 로그인·OTP 후 웹에서 발급·검증·이력·감사 — `license-server/README.md` |
| `docs/sdk/` | **SDK 적용 문서** (빌드·코드 결합·STT 서버·규격·테스트·현장 운영) — `docs/sdk/README.md` |
| `license-server/docs/` | 발급 서버 문서(감사 해시 체인, `licensectl` 발급 측 절차) |
| `scripts/make-sdk-release.sh` | SDK 적용용 패키지 생성(발급 서버 제외). 목록 `scripts/sdk-release-files.txt`, 설명 `docs/sdk/RELEASE_MANIFEST.md` |
| `docs/dev/` | 작업 상태(`STATUS.md`)와 결정 기록(`DECISIONS.md`) — 새 세션·클라우드에서 이어 작업할 때의 출발점 |
| `CLAUDE.md` | Claude Code 세션 안내(읽는 순서, 검증 명령, 금지 사항, 작업 종료 절차) |
| `scripts/check.sh` | 전체 검증 (아래) |
| `scripts/cloud-setup.sh` | 새 Ubuntu·클라우드 환경에 검증 도구 준비(없는 것만 설치) |

## 빌드

필요한 것: CMake 3.16 이상, C++17 컴파일러, OpenSSL 1.1.1 이상(libcrypto), GoogleTest(테스트용), JDK 17 이상(하네스용)

```bash
cmake -S . -B build -G Ninja && cmake --build build
./build/stt_license_tests
# 산출물은 build/ 아래에 둔다 (git 무시 대상). 운영 키는 발급 전용 호스트에서만 만든다.
./build/licensectl keygen -out build/dev-keys
./build/licensectl issue -key build/dev-keys/private.key -in core/testdata/contract.example.json -out build/license.lic
./build/licensectl verify -pub build/dev-keys/public.key build/license.lic
```

SDK에 넣을 때는 다음과 같이 쓴다. 자세한 내용은 `docs/sdk/01-build.md`를 본다.
```cmake
set(STT_LICENSE_PUBLIC_KEY_FILE ${CMAKE_SOURCE_DIR}/keys/stt_license_public.key)   # 운영 공개키 고정 (01-build.md §6)
add_subdirectory(third_party/stt-license-sdk EXCLUDE_FROM_ALL)   # 발행 패키지를 푼 위치
target_link_libraries(sonastt_jni_v2 PRIVATE stt_license::embedded)
stt_license_harden_shared_library(sonastt_jni_v2)
```

## 검증

```bash
scripts/check.sh            # 결과물은 ./build-check
```
1. ASan과 UBSan을 켠 상태에서 단위 테스트를 돌린다.
2. 골든 벡터를 JDK 표준 Ed25519와 Python json으로 교차 검증한다.
3. licensectl을 처음부터 끝까지 실행한다.
4. JNI 하네스를 빌드한다(OpenSSL 정적 링크, `Java_*` 심볼만 export).
5. Java에서 JNI 결합 테스트를 돌린다.
6. LD_PRELOAD로 검증 함수를 가로채는 공격을 시도한다. 보호 빌드는 막아야 하고, 대조군은 뚫려야 한다.
7. SDK를 흉내 낸 CMake 소비자를 빌드한다. 부모 C++14/20, OpenSSL 재사용·사용자 지정 타깃 구성에서 빌드되는지, 키 누락과 비밀키 오지정이 거부되는지 확인한다.

퍼징(clang 필요):
```bash
CC=clang CXX=clang++ cmake -S . -B build-fuzz -DSTT_LICENSE_BUILD_TESTS=OFF -DSTT_LICENSE_BUILD_FUZZ=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fsanitize=fuzzer-no-link"
mkdir -p build-fuzz/corpus && cp core/testdata/* build-fuzz/corpus/   # 시드만 복사, testdata는 건드리지 않음
cmake --build build-fuzz --target fuzz_parse && ./build-fuzz/fuzz_parse -max_total_time=180 build-fuzz/corpus
```
libFuzzer는 새로 찾은 입력을 코퍼스 디렉터리에 계속 저장하므로 `core/testdata`를 코퍼스로 직접 지정하지 않는다.

골든 파일(`core/testdata/golden_*`)은 정규화·출력 규칙이 바뀌지 않았다는 증거다. 이 테스트가 실패하면 이미 배포된 라이선스가 깨질 수 있다는 뜻이므로, 의도한 변경인지 반드시 확인한 뒤에만 `STT_UPDATE_GOLDEN=1`로 다시 생성한다.

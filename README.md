# licenseServer

폐쇄망 STT 제품용 오프라인 라이선스. 사내에서 Ed25519로 서명한 평문 `license.lic`를 한 번 반입하면, STT SDK가 내장 공개키로 스스로 검증하고 채널 수를 제한한다.

| 경로 | 내용 |
|---|---|
| `core/` | `stt_license` C++17 정적 라이브러리. 엄격 JSON 파서, RFC 8785 정규화, 서명 검증, 기간 판정, 채널 한도 |
| `tools/licensectl/` | 사내 발급 CLI (`keygen`, `issue`, `verify`) |
| `integration/harness/` | SDK 호출 모델을 흉내 낸 JNI 하네스 + Java 결합 테스트 |
| `docs/integration.md` | SDK·서버 적용 가이드 |
| `docs/operations.md` | 키 생성·발급·반입·갱신·장애 대응 절차 |
| `scripts/check.sh` | 전체 검증 (아래) |

## 빌드

필요한 것: CMake 3.16 이상, C++17 컴파일러, OpenSSL 1.1.1 이상(libcrypto), GoogleTest(테스트용), JDK 17 이상(하네스용)

```bash
cmake -S . -B build -G Ninja && cmake --build build
./build/stt_license_tests
./build/licensectl keygen -out ./keys
./build/licensectl issue -key keys/private.key -in core/testdata/contract.example.json -out license.lic
./build/licensectl verify -pub keys/public.key license.lic
```

SDK에 넣는 빌드는 `-DSTT_LICENSE_PUBLIC_KEY_FILE=<public.key>`를 지정하고 `stt_license_key`를 링크한다. 자세한 내용은 `docs/integration.md`를 본다.

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

퍼징(clang 필요):
```bash
CC=clang CXX=clang++ cmake -S . -B build-fuzz -DSTT_LICENSE_BUILD_TESTS=OFF -DSTT_LICENSE_BUILD_FUZZ=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fsanitize=fuzzer-no-link"
cmake --build build-fuzz --target fuzz_parse && ./build-fuzz/fuzz_parse -max_total_time=180 core/testdata
```

골든 파일(`core/testdata/golden_*`)은 정규화·출력 규칙이 바뀌지 않았다는 증거다. 이 테스트가 실패하면 이미 배포된 라이선스가 깨질 수 있다는 뜻이므로, 의도한 변경인지 반드시 확인한 뒤에만 `STT_UPDATE_GOLDEN=1`로 다시 생성한다.

#!/usr/bin/env bash
# 전체 검증: 단위 테스트(ASan/UBSan) → 골든 교차검증(JDK/Python) → licensectl E2E → JNI 하네스(정적 OpenSSL, 심볼 은닉)
#           → Java 결합 테스트 → LD_PRELOAD 공격 실험 → SDK 형태 CMake 소비자 빌드
# 사용: scripts/check.sh [build_root]   (기본: ./build-check)
set -euo pipefail
# 주의: pipefail 에서 `cmd | grep -q` 는 grep 이 먼저 끝나 cmd 가 SIGPIPE 를 받으면 "불일치"로 오판된다.
#       검사할 출력은 변수로 받은 뒤 grep <<<"$out" 형태로 쓴다.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
B="${1:-$ROOT/build-check}"
W="$B/work"
rm -rf "$W"; mkdir -p "$W"

step() { printf '\n== %s ==\n' "$*"; }

step "1. 코어 단위 테스트 (ASan+UBSan)"
cmake -S "$ROOT" -B "$B/asan" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" >/dev/null
cmake --build "$B/asan" >/dev/null
"$B/asan/stt_license_tests" | tail -3

step "2. 골든 벡터 교차검증"
java "$ROOT/core/tests/crosscheck/GoldenCheck.java" "$ROOT/core/testdata"
python3 - "$ROOT/core/testdata" <<'PY'
import json, sys
d = sys.argv[1]
lic = json.load(open(f"{d}/golden_license.lic", encoding="utf-8")); lic.pop("signature")
c = json.dumps(lic, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()
assert c == open(f"{d}/golden_canonical.json", "rb").read(), "python canonical mismatch"
print("Python json: canonical bytes match")
PY

step "3. licensectl E2E"
L="$B/asan/licensectl"
"$L" keygen -out "$W/keys" >/dev/null
touch "$W/.perm"; chmod 600 "$W/.perm"
if [ "$(stat -c %a "$W/.perm")" = 600 ]; then
  [ "$(stat -c %a "$W/keys/private.key")" = 600 ] || { echo "private.key mode"; exit 1; }
else
  echo "(이 파일시스템은 POSIX 권한을 지원하지 않아 0600 검사를 건너뜀 — licensectl 이 경고를 출력해야 함)"
  out=$("$L" keygen -out "$W/keys-permcheck" 2>&1 >/dev/null)
  grep -q "WARNING: .*readable by other users" <<<"$out" \
    || { echo "licensectl did not warn about insecure key permissions"; exit 1; }
fi
if "$L" keygen -out "$W/keys" 2>/dev/null; then echo "keygen overwrote keys"; exit 1; fi
mk() {  # mk <name> <offline> <not_before> <not_after>
  sed -e "s/\"offline_stt\": 8/\"offline_stt\": $2/" -e "s/2026-09-21T00:00:00Z/$3/" -e "s/2027-09-20T23:59:59Z/$4/" \
    "$ROOT/core/testdata/contract.example.json" > "$W/$1.json"
  "$L" issue -key "$W/keys/private.key" -in "$W/$1.json" -out "$W/$1.lic" -ledger "$W/ledger" >/dev/null
}
mk valid 3 2020-01-01T00:00:00Z 2099-12-31T23:59:59Z
mk more 5 2020-01-01T00:00:00Z 2099-12-31T23:59:59Z
mk expired 3 2020-01-01T00:00:00Z 2020-12-31T23:59:59Z
sed 's/"offline_stt": 3/"offline_stt": 30/' "$W/valid.lic" > "$W/tampered.lic"
"$L" verify -pub "$W/keys/public.key" "$W/valid.lic" >/dev/null
if "$L" verify -pub "$W/keys/public.key" "$W/tampered.lic" >/dev/null; then echo "tampered verified"; exit 1; fi
if "$L" verify -pub "$W/keys/public.key" "$W/expired.lic" >/dev/null; then echo "expired verified"; exit 1; fi
[ "$(wc -l < "$W/ledger/ledger.jsonl")" = 3 ] || { echo "ledger rows"; exit 1; }
# 키 혼동 방지: 비밀키를 공개키 자리에, 공개키를 발급 키 자리에 쓰면 거부
# (pipefail 이므로 기대된 실패 코드를 파이프에 넣지 않고 출력을 먼저 받는다)
out=$("$L" verify -pub "$W/keys/private.key" "$W/valid.lic" 2>&1 || true)
grep -q "PRIVATE key" <<<"$out" || { echo "private key accepted as public: $out"; exit 1; }
out=$("$L" issue -key "$W/keys/public.key" -in "$W/valid.json" -out "$W/x.lic" 2>&1 || true)
grep -q "not a licensectl private key" <<<"$out" || { echo "public key accepted for signing: $out"; exit 1; }
[ ! -e "$W/x.lic" ] || { echo "license written with public key"; exit 1; }
echo "keygen/issue/verify/ledger OK"

step "4. JNI 하네스 빌드 (정적 OpenSSL, JNI 심볼만 export)"
cmake -S "$ROOT" -B "$B/harness" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTT_LICENSE_BUILD_TESTS=OFF \
  -DSTT_LICENSE_BUILD_HARNESS=ON -DSTT_LICENSE_OPENSSL_STATIC=ON \
  -DSTT_LICENSE_PUBLIC_KEY_FILE="$W/keys/public.key" >/dev/null
cmake --build "$B/harness" --target stt_harness_jni >/dev/null
SO="$B/harness/libstt_harness_jni.so"
if grep -q libcrypto <<<"$(ldd "$SO")"; then echo "libcrypto is dynamically linked"; exit 1; fi
BAD=$(nm -D --defined-only "$SO" | awk '{print $3}' | grep -v '^Java_' || true)
[ -z "$BAD" ] || { echo "unexpected exported symbols:"; echo "$BAD" | head; exit 1; }
echo "exports: $(nm -D --defined-only "$SO" | grep -c ' T Java_') Java_* only, libcrypto static"

step "5. Java ↔ JNI 결합 테스트"
javac -d "$B/classes" "$ROOT"/integration/harness/java/licenseharness/*.java
java -Djava.library.path="$B/harness" -cp "$B/classes" licenseharness.HarnessTest "$W"

step "6. LD_PRELOAD 로 EVP_DigestVerify 가로채기 시도"
cat > "$W/fake_verify.c" <<'C'
int EVP_DigestVerify(void* c, const unsigned char* s, unsigned long sl, const unsigned char* t, unsigned long tl) { return 1; }
C
cc -shared -fPIC -o "$W/fake_verify.so" "$W/fake_verify.c"
cp "$W/tampered.lic" "$W/license.lic"
cat > "$W/Preload.java" <<'J'
public class Preload { public static void main(String[] a) {
  int r = new licenseharness.SttHarness().connect(a[0], 1, 1);
  System.out.println("connect(tampered) under LD_PRELOAD = " + r); System.exit(r == 1004 ? 0 : 1); } }
J
javac -cp "$B/classes" -d "$W" "$W/Preload.java"
echo "[보호 빌드] 정적 OpenSSL + 심볼 은닉 → 가로채기 실패해야 함"
LD_PRELOAD="$W/fake_verify.so" java -Djava.library.path="$B/harness" -cp "$B/classes:$W" Preload "$W/license.lic"
# 대조군: 같은 코드를 동적 libcrypto + 기본 심볼 노출로 빌드하면 공격이 통해야 한다(= 위 결과가 우연이 아님을 증명).
echo "[대조군] 동적 libcrypto, 심볼 노출 → 가로채기 성공해야 함"
mkdir -p "$W/unprotected"
c++ -std=c++17 -O2 -shared -fPIC -I"$ROOT/core/include" -I"$ROOT/core/src" $(dirname "$(dirname "$(readlink -f "$(command -v javac)")")" | sed 's|.*|-I&/include -I&/include/linux|') \
  "$ROOT/integration/harness/harness_jni.cpp" "$ROOT"/core/src/{json,crypto,license,manager}.cpp \
  "$B/harness/generated/stt_license_embedded_key.cpp" -lcrypto -o "$W/unprotected/libstt_harness_jni.so"
if LD_PRELOAD="$W/fake_verify.so" java -Djava.library.path="$W/unprotected" -cp "$B/classes:$W" Preload "$W/license.lic"; then
  echo "control did not bypass: the LD_PRELOAD test is not meaningful"; exit 1
fi
# 대조군 2: 현재 STT SDK 와 같은 구성(OpenSSL 정적 링크, 심볼 export) → 정적 링크만으로는 막히지 않음을 확인.
echo "[대조군 2] 정적 libcrypto + 심볼 export (현재 SDK 구성) → 가로채기 성공해야 함"
mkdir -p "$W/sdklike"
c++ -std=c++17 -O2 -shared -fPIC -I"$ROOT/core/include" -I"$ROOT/core/src" $(dirname "$(dirname "$(readlink -f "$(command -v javac)")")" | sed 's|.*|-I&/include -I&/include/linux|') \
  "$ROOT/integration/harness/harness_jni.cpp" "$ROOT"/core/src/{json,crypto,license,manager}.cpp \
  "$B/harness/generated/stt_license_embedded_key.cpp" -Wl,--whole-archive "$(pkg-config --variable=libdir libcrypto)/libcrypto.a" \
  -Wl,--no-whole-archive -ldl -pthread -o "$W/sdklike/libstt_harness_jni.so"
if grep -q libcrypto <<<"$(ldd "$W/sdklike/libstt_harness_jni.so")"; then echo "sdklike: libcrypto not static"; exit 1; fi
if LD_PRELOAD="$W/fake_verify.so" java -Djava.library.path="$W/sdklike" -cp "$B/classes:$W" Preload "$W/license.lic"; then
  echo "sdklike control did not bypass"; exit 1
fi
echo "대조군 1·2 모두 변조 파일 통과 → 보호 빌드(심볼 은닉)의 차단 효과 확인"

step "7. SDK 형태 CMake 소비자 (add_subdirectory) — 부모 C++ 표준·OpenSSL 제공 방식별"
for std in 11 14 17 20; do
  g++ -std=c++$std -Wall -Wextra -Wpedantic -Werror -I"$ROOT/core/include" -fsyntax-only -x c++ - \
    <<<'#include "stt_license/license.h"' || { echo "public header fails with C++$std"; exit 1; }
done
echo "공개 헤더: C++11/14/17/20 -Wpedantic -Werror OK"
consumer() {  # consumer <name> <cmake args...>
  local name=$1; shift
  local cb="$B/consumer-$name"
  cmake -S "$ROOT/integration/cmake-consumer" -B "$cb" -G Ninja -DSTT_LICENSE_SOURCE_DIR="$ROOT" \
    -DSTT_LICENSE_PUBLIC_KEY_FILE="$W/keys/public.key" "$@" >/dev/null
  cmake --build "$cb" >/dev/null
  local exp; exp=$(nm -D --defined-only "$cb/libmock_sdk_jni.so" | awk '{print $3}' | grep -v '^mock_' || true)
  [ -z "$exp" ] || { echo "[$name] unexpected exports: $exp"; exit 1; }
  if grep -qE '^(licensectl|stt_license_tests):' <<<"$(ninja -C "$cb" -t targets all)"; then
    echo "[$name] tools/tests built as subproject"; exit 1
  fi
  printf '[%s] ' "$name"; "$cb/mock_sdk_smoke" "$W/valid.lic" "$W/tampered.lic"
}
consumer cxx14 -DMOCK_CXX_STANDARD=14
consumer cxx20 -DMOCK_CXX_STANDARD=20
consumer parent_find -DMOCK_CRYPTO_MODE=parent_find
consumer custom_target -DMOCK_CRYPTO_MODE=custom_target \
  -DMOCK_LIBCRYPTO_A="$(pkg-config --variable=libdir libcrypto)/libcrypto.a" \
  -DMOCK_OPENSSL_INCLUDE="$(pkg-config --variable=includedir libcrypto)"
# 키 없이 stt_license::embedded 를 쓰면 명확한 메시지로 빌드가 실패해야 한다
cmake -S "$ROOT/integration/cmake-consumer" -B "$B/consumer-nokey" -G Ninja -DSTT_LICENSE_SOURCE_DIR="$ROOT" >/dev/null
if cmake --build "$B/consumer-nokey" >"$B/consumer-nokey.log" 2>&1 || ! grep -q "STT_LICENSE_PUBLIC_KEY_FILE is not set" "$B/consumer-nokey.log"; then
  echo "missing key was not reported"; exit 1
fi
echo "[nokey] 키 누락 시 명확한 빌드 오류 확인"
# 비밀키를 공개키로 지정하면 구성 단계에서 중단 (비밀키가 SDK 바이너리에 내장·배포되는 사고 방지)
if cmake -S "$ROOT/integration/cmake-consumer" -B "$B/consumer-privkey" -DSTT_LICENSE_SOURCE_DIR="$ROOT" \
     -DSTT_LICENSE_PUBLIC_KEY_FILE="$W/keys/private.key" >"$B/consumer-privkey.log" 2>&1 \
   || ! grep -q "is a PRIVATE key" "$B/consumer-privkey.log"; then
  echo "private key was accepted as STT_LICENSE_PUBLIC_KEY_FILE"; exit 1
fi
echo "[privkey] 비밀키를 공개키로 지정하면 구성 중단 확인"

printf '\nALL CHECKS PASSED\n'

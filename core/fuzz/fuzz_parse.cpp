// libFuzzer: 파서·정규화·검증 경로가 어떤 입력에도 크래시·UB 없이 끝나는지, 정규화가 멱등인지 확인.
#include <cstdint>
#include <cstdlib>
#include <string>

#include "detail.h"
#include "json.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  namespace lic = stt::license;
  const std::string in(reinterpret_cast<const char*>(data), size);

  lic::json::Value v;
  std::string err;
  if (lic::json::Parse(in, &v, &err)) {
    const std::string c1 = lic::json::Canonicalize(v);
    lic::json::Value v2;
    if (!lic::json::Parse(c1, &v2, &err)) std::abort();       // 정규화 결과는 항상 재파싱 가능
    if (lic::json::Canonicalize(v2) != c1) std::abort();      // 멱등
    lic::json::Value v3;
    if (!lic::json::Parse(lic::json::Pretty(v, {"b", "a"}), &v3, &err)) std::abort();
    if (lic::json::Canonicalize(v3) != c1) std::abort();      // Pretty 도 같은 의미
  }

  static const uint8_t kPub[32] = {0};
  lic::LicenseData d;
  lic::ParseAndVerify(in, kPub, &d, &err);
  return 0;
}

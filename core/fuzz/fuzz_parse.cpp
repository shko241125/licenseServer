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

  // 서명 전용 경로 + 값 조회(경로 파싱) — 서명이 맞을 수 없는 입력이지만 파싱·오류 경로를 탄다
  lic::VerifiedLicense verified;
  lic::VerifySignatureWith(in, kPub, &verified, &err);
  std::string s;
  std::int64_t n;
  verified.GetString(in.substr(0, 32), &s);
  verified.GetInt("allowed_channels.offline_stt", &n);
  lic::LicenseFields fields;
  lic::VerifyWith(in, lic::VerifyMode::Full, kPub, 0, &verified, &fields, &err);
  if (size >= 2) {  // UTF-16 변환: 임의 코드 유닛 배열
    std::string u8;
    lic::Utf16ToUtf8(reinterpret_cast<const std::uint16_t*>(data), size / 2, &u8);
  }
  return 0;
}

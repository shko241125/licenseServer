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

  // 서명 단계 + 값 조회(경로 파싱) — 서명이 맞을 수 없는 입력이지만 파싱·오류 경로를 탄다
  lic::VerifiedLicense verified;
  lic::VerifySignatureWith(in, kPub, &verified, &err);
  std::string s;
  std::int64_t n;
  verified.GetString(in.substr(0, 32), &s);
  verified.GetInt("allowed_channels.offline.sonastt", &n);
  lic::LicenseFields fields;
  lic::VerifyLicenseWith(in, kPub, 0, &verified, &fields, &err);
  // 필드 해석 경로: 임의 객체에 고정 테스트 키로 서명을 붙여 검증한다. 위 경로들은 서명이 맞을 수 없어
  // 필드 해석(license_ver·채널 항목·total·기간·Manager 상태)까지 내려가지 못하므로 따로 태운다.
  if (lic::json::Parse(in, &v, &err) && v.type == lic::json::Value::Type::Object && !v.Find("signature")) {
    static uint8_t seed[32];
    static uint8_t pub[32];
    static const bool init = [] {
      for (int i = 0; i < 32; ++i) seed[i] = static_cast<uint8_t>(i + 1);
      return lic::crypto::Ed25519PublicFromSeed(seed, pub);
    }();
    uint8_t sig[64];
    if (init && lic::crypto::Ed25519Sign(seed, lic::json::Canonicalize(v), sig)) {
      v.Set("signature", lic::json::MakeString(lic::crypto::Base64Encode(sig, 64)));
      const std::string text = lic::json::Canonicalize(v);
      lic::LicenseData ld;
      const lic::Error e = lic::ParseAndVerify(text, pub, &ld, &err);
      if (e == lic::Error::BadSignature) std::abort();          // 방금 서명했으므로 서명은 항상 맞아야 한다
      if (e == lic::Error::Ok) {                                // 통과했다면 규칙을 모두 만족해야 한다
        if (ld.license_ver != 1 || ld.total < 1 || ld.total > 100000 || ld.not_before >= ld.not_after) std::abort();
        bool positive = false;
        if (ld.groups.size() > 64 || ld.groups.count("total")) std::abort();
        for (const auto& [group, items] : ld.groups) {
          if (items.size() > 64) std::abort();
          for (const auto& [name, cap] : items) {
            if (cap < 0 || cap > ld.total) std::abort();
            positive = positive || cap > 0;
          }
        }
        if (!positive) std::abort();
        (void)ld.StateAt(0);
        lic::VerifiedLicense vl;
        lic::LicenseFields lf;
        if (lic::VerifySignatureWith(text, pub, &vl, &err) != lic::Error::Ok ||
            lic::ParseStandardFields(vl, &lf, &err) != lic::Error::Ok || lf.total_channels != ld.total || lf.channels != ld.groups) {
          std::abort();                                         // 두 경로의 판정이 같아야 한다
        }
      }
    }
  }
  if (size >= 2) {  // UTF-16 변환: 임의 코드 유닛 배열
    std::string u8;
    lic::Utf16ToUtf8(reinterpret_cast<const std::uint16_t*>(data), size / 2, &u8);
  }
  return 0;
}

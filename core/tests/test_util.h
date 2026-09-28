#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <string>

#include "crypto.h"
#include "detail.h"
#include "json.h"

namespace testutil {

namespace lic = stt::license;
namespace crypto = stt::license::crypto;

// 테스트용 고정 seed (0x01..0x20). 운영 키가 아니다.
inline std::array<uint8_t, 32> TestSeed(uint8_t base = 1) {
  std::array<uint8_t, 32> s{};
  for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<uint8_t>(base + i);
  return s;
}

inline std::array<uint8_t, 32> PublicOf(const std::array<uint8_t, 32>& seed) {
  std::array<uint8_t, 32> pub{};
  crypto::Ed25519PublicFromSeed(seed.data(), pub.data());
  return pub;
}

struct Contract {
  std::string license_id = "0123456789abcdef0123456789abcdef";
  std::string project_name = "STT Platform Modernization";
  std::string license_type = "production";
  std::string site_id = "SEOUL-MAIN-DC";
  int online = 16, offline = 8;
  std::string issued_at = "2026-09-21T00:00:00Z";
  std::string not_before = "2026-09-21T00:00:00Z";
  std::string not_after = "2027-09-20T23:59:59Z";
  int grace = 14;
  std::string warning_notice =
      "This software license is geographically restricted to the designated site. "
      "Unauthorized replication or exceeding allowed channels is strictly prohibited.";

  std::string Json() const {
    return "{\"format_version\":1,\"license_id\":\"" + license_id + "\",\"project_name\":\"" +
           project_name + "\",\"license_type\":\"" + license_type + "\",\"site_id\":\"" +
           site_id + "\",\"allowed_channels\":{\"online_stt\":" + std::to_string(online) +
           ",\"offline_stt\":" + std::to_string(offline) + "},\"validity\":{\"issued_at\":\"" +
           issued_at + "\",\"not_before\":\"" + not_before + "\",\"not_after\":\"" + not_after +
           "\",\"grace_period_days\":" + std::to_string(grace) + "},\"warning_notice\":\"" +
           warning_notice + "\"}";
  }
};

// 첫 번째 일치 부분을 바꾼다. 없으면 테스트 실패.
inline std::string Replace(std::string s, const std::string& from, const std::string& to) {
  size_t p = s.find(from);
  EXPECT_NE(p, std::string::npos) << "pattern not found: " << from;
  if (p != std::string::npos) s.replace(p, from.size(), to);
  return s;
}

// SignContract 로 발급 (스키마 검증 포함).
inline std::string Issue(const Contract& c, const std::array<uint8_t, 32>& seed = TestSeed()) {
  std::string text, detail;
  lic::Error e = lic::SignContract(c.Json(), seed.data(), 0, &text, nullptr, &detail);
  if (e != lic::Error::Ok) {
    ADD_FAILURE() << "SignContract failed: " << lic::ErrorName(e) << " " << detail;
  }
  return text;
}

// 스키마 검증 없이 임의 JSON 트리에 서명을 붙인다 (검증기 거부 경로 테스트용).
inline std::string SignRaw(const std::string& json_without_sig,
                           const std::array<uint8_t, 32>& seed = TestSeed()) {
  lic::json::Value root;
  std::string err;
  if (!lic::json::Parse(json_without_sig, &root, &err)) {
    ADD_FAILURE() << "SignRaw parse: " << err;
    return "";
  }
  uint8_t sig[64];
  crypto::Ed25519Sign(seed.data(), lic::json::Canonicalize(root), sig);
  root.Set("signature", lic::json::MakeString(crypto::Base64Encode(sig, 64)));
  return lic::json::Canonicalize(root);
}

inline int64_t T(const char* s) {
  int64_t t = 0;
  EXPECT_TRUE(lic::ParseUtcTime(s, &t)) << s;
  return t;
}

class TempDir {
 public:
  TempDir() {
    std::string tmpl = (std::filesystem::temp_directory_path() / "stt_license_XXXXXX").string();
    char* p = ::mkdtemp(tmpl.data());
    path_ = p ? p : "";
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  std::string File(const std::string& name) const { return path_ + "/" + name; }

 private:
  std::string path_;
};

// 운영에서처럼 새 파일을 만든 뒤 rename 으로 교체한다 (inode 변경).
inline void ReplaceFile(const std::string& path, const std::string& data) {
  const std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    f << data;
  }
  std::filesystem::rename(tmp, path);
}

}  // namespace testutil

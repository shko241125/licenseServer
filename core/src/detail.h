// 내부 헤더: 코어·발급기·테스트 전용. SDK에 배포하지 않는다.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "crypto.h"
#include "stt_license/license.h"

namespace stt::license {

constexpr size_t kMaxFileSize = 64 * 1024;
constexpr int kFormatVersion = 1;
constexpr int kMaxChannels = 100000;
constexpr int kMaxGraceDays = 90;

// 서명 검증을 통과한 뒤에만 채워지는 라이선스 내용.
struct LicenseData {
  int format_version = 0;
  std::string license_id, project_name, license_type, site_id, warning_notice;
  int online = 0, offline = 0;
  int64_t issued_at = 0, not_before = 0, not_after = 0;
  int grace_days = 0;

  int64_t GraceUntil() const { return not_after + int64_t{grace_days} * 86400; }
  State StateAt(int64_t now) const;
  int Max(Kind k) const { return k == Kind::Online ? online : offline; }
};

// 파일 바이트 → 서명 검증 → 스키마 검증. detail에는 사람이 읽을 사유가 들어간다.
Error ParseAndVerify(std::string_view bytes, const uint8_t pub[crypto::kPublicKeySize],
                     LicenseData* out, std::string* detail);

// 발급기 전용: 계약 JSON(서명 없음)에 기본값을 채우고 검증·서명한 뒤 사람이 읽는 license.lic 텍스트를 만든다.
//   format_version 없음 → 1, license_id 없음 → 난수 32 hex, validity.issued_at 없음 → now
// 발급 직후 같은 키의 공개키로 ParseAndVerify 자기검증까지 통과해야 성공.
Error SignContract(std::string_view contract_json, const uint8_t seed[crypto::kSeedSize],
                   int64_t now, std::string* license_text, LicenseData* out, std::string* detail);

// "YYYY-MM-DDTHH:MM:SSZ" (정확히 20자, UTC) ↔ epoch 초.
bool ParseUtcTime(std::string_view s, int64_t* out);
std::string FormatUtcTime(int64_t t);

int64_t SystemNow();

namespace detail {
struct Config {
  std::array<uint8_t, crypto::kPublicKeySize> public_key{};
  std::function<int64_t()> now = SystemNow;
  int64_t recheck_seconds = 60;
};
}  // namespace detail

}  // namespace stt::license

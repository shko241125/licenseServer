// 내부 헤더: 코어·발급기·테스트 전용. SDK에 배포하지 않는다.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "crypto.h"
#include "json.h"
#include "stt_license/license.h"

namespace stt::license {

constexpr size_t kMaxFileSize = 64 * 1024;
constexpr int kLicenseVer = 1;          // license_ver (형식 버전). 이전 형식(format_version)은 지원하지 않는다
constexpr int kMaxChannels = 100000;
constexpr size_t kMaxGroups = 64;       // allowed_channels 의 항목 수 상한 (total 제외)
constexpr size_t kMaxEngines = 64;      // 항목 하나의 세부 항목 수 상한
// 운영(production) 라이선스의 영구 표기. 발급 정책에만 쓰고, 검증기는 일반 시각으로 다룬다.
constexpr const char* kPermanentNotAfter = "9999-12-31T23:59:59Z";

// 서명 검증을 통과한 뒤에만 채워지는 라이선스 내용.
struct LicenseData {
  int license_ver = 0;
  std::string license_id, project_name, license_type, solution, site_id, warning_notice;
  int total = 0;  // 전체 동시 세션 상한(모든 항목·세부 항목 합)
  std::map<std::string, std::map<std::string, int>> groups;  // 항목 → 세부 항목 → 상한(각 값 ≤ total)
  int64_t issued_at = 0, not_before = 0, not_after = 0;

  State StateAt(int64_t now) const;
  int Max(const std::string& group, const std::string& engine) const {  // 라이선스에 없으면 0
    auto g = groups.find(group);
    if (g == groups.end()) return 0;
    auto it = g->second.find(engine);
    return it == g->second.end() ? 0 : it->second;
  }
};

// 파일 바이트 → 서명 검증 → 스키마 검증. detail에는 사람이 읽을 사유가 들어간다.
Error ParseAndVerify(std::string_view bytes, const uint8_t pub[crypto::kPublicKeySize],
                     LicenseData* out, std::string* detail);

// 발급기 전용: 계약 JSON(서명 없음)에 기본값을 채우고 검증·서명한 뒤 사람이 읽는 license.lic 텍스트를 만든다.
//   license_ver 없음 → 1, license_id 없음 → 난수 32 hex, validity.issued_at 없음 → now
// 발급 정책: license_type 은 production/trial/dev, production 의 not_after 는 영구 표기(kPermanentNotAfter),
//            trial/dev 는 영구 표기 금지. 이전 형식 키(format_version, grace_period_days, online_stt, offline_stt)는 거부.
// 발급 직후 같은 키의 공개키로 ParseAndVerify 자기검증까지 통과해야 성공.
Error SignContract(std::string_view contract_json, const uint8_t seed[crypto::kSeedSize],
                   int64_t now, std::string* license_text, LicenseData* out, std::string* detail);

// "YYYY-MM-DDTHH:MM:SSZ" (정확히 20자, UTC) ↔ epoch 초.
bool ParseUtcTime(std::string_view s, int64_t* out);
std::string FormatUtcTime(int64_t t);

int64_t SystemNow();

// 서명만 검증한다(표준 필드 해석 없음). root 는 signature 를 뺀 트리, canonical 은 서명 대상 바이트.
Error VerifyTree(std::string_view bytes, const uint8_t pub[crypto::kPublicKeySize], json::Value* root,
                 std::string* canonical, std::string* detail);

// VerifySignature 의 공개키 지정판(테스트·도구용). 배포 빌드는 내장 키를 쓰는 VerifySignature 를 쓴다.
Error VerifySignatureWith(const std::string& license_text, const uint8_t pub[crypto::kPublicKeySize],
                          VerifiedLicense* out, std::string* detail);

// 시각·공개키 지정판(테스트용). 배포 빌드는 내장 키와 시스템 시각을 쓰는 VerifyLicense / Verify 를 쓴다.
Error VerifyLicenseWith(const std::string& license_text, const uint8_t pub[crypto::kPublicKeySize], int64_t now,
                        VerifiedLicense* out, LicenseFields* fields, std::string* detail);
Error VerifyWith(const std::string& license_text, VerifyMode mode, const uint8_t pub[crypto::kPublicKeySize],
                 int64_t now, VerifiedLicense* out, LicenseFields* fields, std::string* detail);

namespace detail {
struct VerifiedData {
  json::Value root;       // signature 를 뺀, 서명으로 보증된 트리
  std::string canonical;  // 서명 대상 바이트
};

struct VerifiedAccess {
  static void Set(VerifiedLicense* v, std::shared_ptr<const VerifiedData> d) { v->data_ = std::move(d); }
  static const VerifiedData* Get(const VerifiedLicense& v) { return v.data_.get(); }
};

struct Config {
  std::array<uint8_t, crypto::kPublicKeySize> public_key{};
  std::function<int64_t()> now = SystemNow;
  int64_t recheck_seconds = 60;
};
}  // namespace detail

}  // namespace stt::license

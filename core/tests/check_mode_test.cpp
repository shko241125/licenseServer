// check_mode 판정(D46·D47): 키 없음·"full" = 전체 규칙, "minimal" + 모르는 license_ver = 안정 필드·기간만,
// "minimal" + 아는 license_ver = 그 판의 전체 규칙. 예시 번호(M1~M26)는 docs/dev/forward-compat.md §8.
#include <gtest/gtest.h>

#include "test_util.h"

namespace {

using namespace testutil;
using lic::CheckMode;
using lic::Error;

const int64_t kNow = T("2026-10-19T00:00:00Z");

// 판 2(이 코어가 모르는 판) minimal 파일의 계약 JSON. 기본 계약은 2026-09-21 ~ 2027-09-20T23:59:59Z.
std::string MinimalJson(const Contract& c = Contract::Dated()) {
  return Replace(c.Json(), "\"license_ver\":1,", "\"check_mode\":\"minimal\",\"license_ver\":2,");
}

Error Judge(const std::string& text, lic::LicenseFields* f = nullptr, std::string* detail = nullptr,
            int64_t now = kNow) {
  auto pub = PublicOf(TestSeed());
  lic::VerifiedLicense v;
  lic::LicenseFields tmp;
  std::string d;
  return lic::VerifyLicenseWith(text, pub.data(), now, &v, f ? f : &tmp, detail ? detail : &d);
}

TEST(CheckMode, AbsentOrFullIsTheV1Judgment) {
  lic::LicenseFields a, b;
  ASSERT_EQ(Judge(Issue(Contract::Dated()), &a), Error::Ok);
  EXPECT_EQ(a.check_mode, CheckMode::Full);
  const std::string full_json =
      Replace(Contract::Dated().Json(), "\"license_ver\":1,", "\"check_mode\":\"full\",\"license_ver\":1,");
  ASSERT_EQ(Judge(SignRaw(full_json), &b), Error::Ok);  // M25
  EXPECT_EQ(b.check_mode, CheckMode::Full);
  EXPECT_EQ(b.site_id, a.site_id);
  EXPECT_EQ(b.channels, a.channels);
  // M24: "full" 은 버전을 본다 / 키가 없는 판 2 도 마찬가지
  EXPECT_EQ(Judge(SignRaw(Replace(full_json, "\"license_ver\":1", "\"license_ver\":3"))), Error::UnsupportedVersion);
  EXPECT_EQ(Judge(SignRaw(Replace(Contract::Dated().Json(), "\"license_ver\":1,", "\"license_ver\":2,"))),
            Error::UnsupportedVersion);
}

TEST(CheckMode, MinimalOnUnknownVersionJudgesStableFieldsOnly) {
  // M1: 판 2 + 모르는 새 키(제한 필드라도 이 코어는 집행하지 않음)
  const std::string text = SignRaw(Replace(MinimalJson(), "\"warning_notice\"", "\"host_binding\":\"srv-a\",\"warning_notice\""));
  lic::LicenseFields f;
  std::string detail;
  ASSERT_EQ(Judge(text, &f, &detail), Error::Ok) << detail;
  EXPECT_TRUE(detail.empty()) << detail;  // 성공하면 사유를 남기지 않음
  EXPECT_EQ(f.check_mode, CheckMode::Minimal);
  EXPECT_EQ(f.license_ver, 2);
  EXPECT_EQ(f.license_id, "0123456789abcdef0123456789abcdef");
  EXPECT_EQ(f.total_channels, 16);
  EXPECT_EQ(f.channels.at("offline").at("sonastt"), 8);
  EXPECT_EQ(f.not_before, T("2026-09-21T00:00:00Z"));
  EXPECT_EQ(f.not_after, T("2027-09-20T23:59:59Z"));
  EXPECT_EQ(f.issued_at, T("2026-09-21T00:00:00Z"));
  EXPECT_EQ(f.site_id, "SEOUL-MAIN-DC");  // 형식이 맞는 표시 필드는 채움
  EXPECT_EQ(f.license_type, "dev");
}

TEST(CheckMode, MinimalDisplayFieldsAreLoose) {
  // M2: 표시 필드 없음 → Ok, 빈 값
  std::string json = MinimalJson();
  for (const char* key : {"\"project_name\":\"STT Platform Modernization\",", "\"license_type\":\"dev\",",
                          "\"solution\":\"sonastt-universal\",", "\"site_id\":\"SEOUL-MAIN-DC\","}) {
    json = Replace(json, key, "");
  }
  json = Replace(json, ",\"warning_notice\":\"" + Contract{}.warning_notice + "\"", "");
  lic::LicenseFields f;
  std::string detail;
  ASSERT_EQ(Judge(SignRaw(json), &f, &detail), Error::Ok) << detail;
  EXPECT_TRUE(detail.empty()) << detail;
  EXPECT_EQ(f.project_name, "");
  EXPECT_EQ(f.license_type, "");
  EXPECT_EQ(f.solution, "");
  EXPECT_EQ(f.site_id, "");
  EXPECT_EQ(f.warning_notice, "");

  // M3: 형식이 다른 표시 필드 → 빈 값, 검증기가 제한하지 않는 값은 그대로(표시만)
  std::string m3 = Replace(MinimalJson(), "\"site_id\":\"SEOUL-MAIN-DC\"", "\"site_id\":123");
  m3 = Replace(m3, "\"license_type\":\"dev\"", "\"license_type\":\"beta\"");
  m3 = Replace(m3, "\"solution\":\"sonastt-universal\"", "\"solution\":\"\"");
  detail.clear();
  ASSERT_EQ(Judge(SignRaw(m3), &f, &detail), Error::Ok) << detail;
  EXPECT_TRUE(detail.empty()) << detail;
  EXPECT_EQ(f.site_id, "");
  EXPECT_EQ(f.license_type, "beta");
  EXPECT_EQ(f.solution, "");

  // M21: license_ver 가 문자열·없음·int 범위 밖 → 0
  for (const char* lv : {"\"license_ver\":\"3\",", "", "\"license_ver\":3000000000,"}) {
    ASSERT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"license_ver\":2,", lv)), &f), Error::Ok) << lv;
    EXPECT_EQ(f.license_ver, 0) << lv;
    EXPECT_EQ(f.check_mode, CheckMode::Minimal) << lv;
  }
  // M22: issued_at 없음·형식 다름 → 0
  for (const char* ia : {"", "\"issued_at\":\"2026-09-21\","}) {
    ASSERT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"issued_at\":\"2026-09-21T00:00:00Z\",", ia)), &f), Error::Ok);
    EXPECT_EQ(f.issued_at, 0) << ia;
  }
}

TEST(CheckMode, CheckModeValueRules) {
  auto with = [](const std::string& value) {
    return SignRaw(Replace(MinimalJson(), "\"check_mode\":\"minimal\"", "\"check_mode\":" + value));
  };
  std::string detail;
  EXPECT_EQ(Judge(with("\"Minimal\""), nullptr, &detail), Error::UnsupportedVersion);  // M4 대소문자 구분
  EXPECT_NE(detail.find("check_mode"), std::string::npos) << detail;
  EXPECT_EQ(Judge(with("\"strict\"")), Error::UnsupportedVersion);  // 모르는 수준 → 실패 폐쇄
  EXPECT_EQ(Judge(with("1")), Error::InvalidField);                 // M23
  EXPECT_EQ(Judge(with("true")), Error::InvalidField);
  EXPECT_EQ(Judge(with("{}")), Error::InvalidField);
}

TEST(CheckMode, MinimalStillRejectsStableFieldViolations) {
  std::string detail;
  // M5: 서명된 full 파일에 check_mode 를 끼워 넣음
  std::string issued = Issue(Contract::Dated());
  EXPECT_EQ(Judge(Replace(issued, "\"license_ver\": 1,", "\"check_mode\": \"minimal\", \"license_ver\": 2,")),
            Error::BadSignature);
  // M6: license_id 없음·129바이트·제어문자
  EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"license_id\":\"0123456789abcdef0123456789abcdef\",", "")),
                  nullptr, &detail), Error::InvalidField);
  EXPECT_NE(detail.find("license_id"), std::string::npos) << detail;
  Contract long_id = Contract::Dated();
  long_id.license_id = std::string(129, 'a');
  EXPECT_EQ(Judge(SignRaw(MinimalJson(long_id))), Error::InvalidField);
  Contract ctl_id = Contract::Dated();
  ctl_id.license_id = "ab\\tcd";
  EXPECT_EQ(Judge(SignRaw(MinimalJson(ctl_id))), Error::InvalidField);
  // M7·M8·M9·M10: 채널
  EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"total\":16", "\"total\":200000"))), Error::InvalidField);
  EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"sonastt\":8", "\"sonastt\":20"))), Error::InvalidField);
  EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"total\":16,", "\"total\":16,\"burst\":2,"))), Error::InvalidField);
  Contract zero = Contract::Dated();
  zero.offline = {{"sonastt", 0}};
  zero.online = {};
  EXPECT_EQ(Judge(SignRaw(MinimalJson(zero)), nullptr, &detail), Error::InvalidField);
  EXPECT_NE(detail.find("at least one"), std::string::npos) << detail;
  EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"allowed_channels\":{", "\"allowed_channels\":{\"x\":{},"))),
            Error::Ok);  // 빈 항목 객체는 v1 과 같이 허용
  // M11·M12: 시각 형식과 순서
  for (const char* na : {"\"2027-01-01\"", "\"2027-01-01T00:00:00.000Z\"", "\"2027-01-01T09:00:00+09:00\""}) {
    EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"not_after\":\"2027-09-20T23:59:59Z\"",
                                    std::string("\"not_after\":") + na))), Error::InvalidField) << na;
  }
  EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), "\"not_after\":\"2027-09-20T23:59:59Z\"",
                                  "\"not_after\":\"2026-09-21T00:00:00Z\""))), Error::InvalidField);
  EXPECT_EQ(Judge(SignRaw(Replace(MinimalJson(), ",\"not_before\":\"2026-09-21T00:00:00Z\"", ""))), Error::InvalidField);
  // M15: 새 필드에 배열 → 서명보다 먼저 문법 단계에서 거부(기존 .key 분기 조건과 같은 ParseError)
  EXPECT_EQ(Judge(Replace(SignRaw(MinimalJson()), "\"license_id\"", "\"features\":[\"a\"],\"license_id\"")),
            Error::ParseError);
}

TEST(CheckMode, MinimalEnforcesPeriod) {
  const std::string text = SignRaw(MinimalJson());
  EXPECT_EQ(Judge(text, nullptr, nullptr, T("2026-09-20T23:59:59Z")), Error::NotYetValid);  // M13
  EXPECT_EQ(Judge(text, nullptr, nullptr, T("2027-09-20T23:59:59Z")), Error::Ok);           // 종료 시각 포함
  EXPECT_EQ(Judge(text, nullptr, nullptr, T("2027-09-21T00:00:00Z")), Error::Expired);      // M14
}

// M26 · R13 · R17: 아는 판(1)이면 minimal 이어도 v1 전체 규칙, 적용 수준은 Full
TEST(CheckMode, MinimalOnKnownVersionUsesFullRules) {
  const std::string v1_minimal =
      Replace(Contract::Dated().Json(), "\"license_ver\":1,", "\"check_mode\":\"minimal\",\"license_ver\":1,");
  lic::LicenseFields f;
  ASSERT_EQ(Judge(SignRaw(v1_minimal), &f), Error::Ok);
  EXPECT_EQ(f.check_mode, CheckMode::Full);
  EXPECT_EQ(Judge(SignRaw(Replace(v1_minimal, "\"site_id\":\"SEOUL-MAIN-DC\"", "\"site_id\":123"))), Error::InvalidField);
  EXPECT_EQ(Judge(SignRaw(Replace(v1_minimal, "\"solution\":\"sonastt-universal\",", ""))), Error::InvalidField);
}

// A10: full 은 모르는 최상위·validity 키, 새 최상위 객체를 계속 무시한다(추가형 진화의 전제)
TEST(CheckMode, FullKeepsIgnoringUnknownKeys) {
  std::string json = Replace(Contract::Dated().Json(), "\"warning_notice\"",
                             "\"new_limit\":3,\"new_obj\":{\"a\":{\"b\":1}},\"warning_notice\"");
  json = Replace(json, "\"not_before\"", "\"grace_note\":\"x\",\"not_before\"");
  lic::LicenseFields f;
  ASSERT_EQ(Judge(SignRaw(json), &f), Error::Ok);
  EXPECT_EQ(f.check_mode, CheckMode::Full);
}

// I3: 발급기는 check_mode 를 넣지 않는다(계약에 있으면 엄격 발급이 거부)
TEST(CheckMode, IssuerRejectsCheckModeInContract) {
  std::string text, detail;
  const std::string contract =
      Replace(Contract::Dated().Json(), "\"license_ver\":1,", "\"check_mode\":\"minimal\",\"license_ver\":1,");
  EXPECT_EQ(lic::SignContract(contract, TestSeed().data(), 0, &text, nullptr, &detail), Error::InvalidField);
  EXPECT_NE(detail.find("check_mode"), std::string::npos) << detail;
  EXPECT_EQ(Issue(Contract::Dated()).find("check_mode"), std::string::npos);
}

TEST(CheckMode, LibraryInfo) {
  EXPECT_STREQ(lic::LibraryVersion(), "3.0.0");
  EXPECT_EQ(lic::MaxLicenseVer(), 1);
  EXPECT_STREQ(lic::CheckModeName(CheckMode::Full), "full");
  EXPECT_STREQ(lic::CheckModeName(CheckMode::Minimal), "minimal");
}

// 내부 ExtractMinimal: 판 2 발급의 자기 검증(I5)이 직접 부르는 고정 판정. 아는 판이라도 최소 판정만 한다.
TEST(CheckMode, ExtractMinimalIsCallableDirectly) {
  lic::json::Value root;
  std::string err;
  ASSERT_TRUE(lic::json::Parse(Contract::Dated().Json(), &root, &err)) << err;
  lic::LicenseData d;
  ASSERT_EQ(lic::ExtractMinimal(root, &d, &err), Error::Ok) << err;
  EXPECT_EQ(d.check_mode, CheckMode::Minimal);
  EXPECT_EQ(d.license_ver, 1);
  root.Erase("license_id");
  EXPECT_EQ(lic::ExtractMinimal(root, &d, &err), Error::InvalidField);
}

}  // namespace

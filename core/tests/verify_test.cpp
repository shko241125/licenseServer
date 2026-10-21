// 문자열 판정 API (VerifyLicense / VerifiedLicense / StateAt / Utf16ToUtf8) 와 그 첫 단계인 서명 단계(내부 VerifySignatureWith)
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "test_util.h"

namespace {

using namespace testutil;
using lic::Error;

lic::VerifiedLicense VerifyOk(const std::string& text, const std::array<uint8_t, 32>& seed = TestSeed()) {
  lic::VerifiedLicense v;
  std::string detail;
  auto pub = PublicOf(seed);
  EXPECT_EQ(lic::VerifySignatureWith(text, pub.data(), &v, &detail), Error::Ok) << detail;
  return v;
}

Error Verify(const std::string& text, lic::VerifiedLicense* v, const std::array<uint8_t, 32>& seed = TestSeed()) {
  auto pub = PublicOf(seed);
  std::string detail;
  return lic::VerifySignatureWith(text, pub.data(), v, &detail);
}

std::u16string U16(const std::string& utf8) {  // 테스트용 UTF-8 → UTF-16 (입력은 유효하다고 가정)
  std::u16string out;
  for (size_t i = 0; i < utf8.size();) {
    unsigned char c = static_cast<unsigned char>(utf8[i]);
    uint32_t cp;
    int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < len; ++k) cp = (cp << 6) | (static_cast<unsigned char>(utf8[i + k]) & 0x3F);
    i += len;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
  }
  return out;
}

bool ToUtf8(const std::u16string& s, std::string* out) {
  return lic::Utf16ToUtf8(reinterpret_cast<const std::uint16_t*>(s.data()), s.size(), out);
}

TEST(SignatureStage, ValuesComeOnlyFromSignedTree) {
  lic::VerifiedLicense v = VerifyOk(Issue(Contract{}));
  ASSERT_TRUE(v.valid());
  std::string s;
  std::int64_t n = 0;
  EXPECT_TRUE(v.GetString("site_id", &s));
  EXPECT_EQ(s, "SEOUL-MAIN-DC");
  EXPECT_TRUE(v.GetInt("allowed_channels.total", &n));
  EXPECT_EQ(n, 16);
  EXPECT_TRUE(v.GetInt("allowed_channels.offline.sonaml", &n));
  EXPECT_EQ(n, 4);
  EXPECT_TRUE(v.GetInt("allowed_channels.online.sonaasr", &n));
  EXPECT_EQ(n, 2);
  EXPECT_FALSE(v.GetInt("allowed_channels.online.sonaml", &n));   // 온라인에는 없는 엔진
  EXPECT_TRUE(v.GetString("solution", &s));
  EXPECT_EQ(s, "sonastt-universal");
  EXPECT_TRUE(v.GetString("validity.not_after", &s));
  EXPECT_EQ(s, "9999-12-31T23:59:59Z");
  EXPECT_TRUE(v.Has("allowed_channels"));
  EXPECT_FALSE(v.Has("signature"));                       // 서명 값 자체는 노출하지 않음
  EXPECT_FALSE(v.GetString("allowed_channels", &s));      // 객체는 문자열이 아님
  EXPECT_FALSE(v.GetInt("site_id", &n));                  // 타입 불일치
  EXPECT_FALSE(v.GetInt("allowed_channels.nope", &n));
  EXPECT_FALSE(v.GetInt("site_id.x", &n));                // 중간이 객체가 아님
  EXPECT_FALSE(v.Has(""));
  EXPECT_FALSE(v.Has("allowed_channels."));
  EXPECT_EQ(v.canonical_json().find("signature"), std::string::npos);
}

TEST(SignatureStage, CanonicalJsonMatchesGolden) {
  const std::string dir = STT_TESTDATA_DIR;
  auto read = [&](const std::string& name) {
    std::ifstream f(dir + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  };
  lic::VerifiedLicense v = VerifyOk(read("golden_license.lic"));
  EXPECT_EQ(v.canonical_json(), read("golden_canonical.json"));  // 서명 대상 바이트 그대로
}

// 서명 단계는 값 규칙을 보지 않는다. 그래서 공개 API 에 서명만 보는 함수를 두지 않고, VerifyLicense 가 거부한다.
TEST(SignatureStage, NonStandardValuesPassSignatureButVerifyLicenseRejects) {
  const std::string odd = SignRaw(
      "{\"license_ver\":7,\"allowed_channels\":{\"total\":999999},"
      "\"validity\":{\"not_after\":\"sometime\"},\"custom\":{\"beta_mode\":true}}");
  lic::VerifiedLicense v = VerifyOk(odd);
  std::int64_t n = 0;
  bool b = false;
  EXPECT_TRUE(v.GetInt("allowed_channels.total", &n));
  EXPECT_EQ(n, 999999);
  EXPECT_TRUE(v.GetBool("custom.beta_mode", &b));
  EXPECT_TRUE(b);
  lic::LicenseFields f;
  std::string detail;
  EXPECT_EQ(lic::ParseStandardFields(v, &f, &detail), Error::UnsupportedVersion) << detail;

  const std::string missing = SignRaw(Replace(Contract{}.Json(), "\"site_id\":\"SEOUL-MAIN-DC\",", ""));
  lic::VerifiedLicense v2 = VerifyOk(missing);
  EXPECT_EQ(lic::ParseStandardFields(v2, &f, &detail), Error::InvalidField);
  EXPECT_NE(detail.find("site_id"), std::string::npos);

  auto pub = PublicOf(TestSeed());
  const int64_t now = T("2027-01-01T00:00:00Z");
  lic::VerifiedLicense out;
  EXPECT_EQ(lic::VerifyLicenseWith(odd, pub.data(), now, &out, &f, &detail), Error::UnsupportedVersion);
  EXPECT_EQ(lic::VerifyLicenseWith(missing, pub.data(), now, &out, &f, &detail), Error::InvalidField);
  const std::string no_ver = SignRaw(Replace(Contract::Dated().Json(), "\"license_ver\":1,", ""));
  lic::VerifiedLicense sig_only, full;
  EXPECT_EQ(Verify(no_ver, &sig_only), Error::Ok);                               // 서명 단계는 통과
  EXPECT_EQ(lic::VerifyLicenseWith(no_ver, pub.data(), now, &full, &f, &detail), Error::InvalidField);
  EXPECT_FALSE(full.valid());                                                    // 실패하면 결과를 채우지 않음
}

TEST(SignatureStage, Rejections) {
  const std::string text = Issue(Contract{});
  lic::VerifiedLicense v;
  EXPECT_EQ(Verify(Replace(text, "\"sonastt\": 8", "\"sonastt\": 16"), &v), Error::BadSignature);
  EXPECT_FALSE(v.valid());                                                   // 실패하면 결과를 채우지 않음
  EXPECT_EQ(Verify(text, &v, TestSeed(77)), Error::BadSignature);            // 다른 키
  EXPECT_EQ(Verify(std::string(lic::kMaxFileSize + 1, ' '), &v), Error::FileTooLarge);
  EXPECT_EQ(Verify("{not json", &v), Error::ParseError);
  EXPECT_EQ(Verify("[1]", &v), Error::ParseError);
  EXPECT_EQ(Verify("{\"a\":1}", &v), Error::BadSignature);                   // 서명 없음
  EXPECT_FALSE(v.valid());
  // 성공 후 실패해도 이전 성공 결과를 덮어쓰지 않는다(호출자가 반환값으로 판단)
  lic::VerifiedLicense ok = VerifyOk(text);
  EXPECT_EQ(Verify("{}", &ok), Error::BadSignature);
  EXPECT_TRUE(ok.valid());
}

TEST(SignatureStage, EmptyObjectIsSafe) {
  lic::VerifiedLicense v;
  std::string s;
  std::int64_t n;
  EXPECT_FALSE(v.valid());
  EXPECT_EQ(v.canonical_json(), "");
  EXPECT_FALSE(v.GetString("site_id", &s));
  EXPECT_FALSE(v.GetInt("x", &n));
  lic::LicenseFields f;
  EXPECT_EQ(lic::ParseStandardFields(v, &f), Error::InvalidField);
}

TEST(SignatureStage, StandardFieldsAndState) {
  lic::VerifiedLicense v = VerifyOk(Issue(Contract{}));
  lic::LicenseFields f;
  ASSERT_EQ(lic::ParseStandardFields(v, &f), Error::Ok);
  EXPECT_EQ(f.license_ver, 1);
  EXPECT_EQ(f.site_id, "SEOUL-MAIN-DC");
  EXPECT_EQ(f.solution, "sonastt-universal");
  EXPECT_EQ(f.total_channels, 16);
  EXPECT_EQ(f.channels, (std::map<std::string, std::map<std::string, int>>{
                            {"offline", {{"sonastt", 8}, {"sonaml", 4}, {"sonaasr", 4}}},
                            {"online", {{"sonastt", 8}, {"sonaasr", 2}}}}));
  EXPECT_EQ(f.not_after, T("9999-12-31T23:59:59Z"));

  lic::VerifiedLicense tv = VerifyOk(Issue(Contract::Dated()));
  ASSERT_EQ(lic::ParseStandardFields(tv, &f), Error::Ok);
  const std::int64_t na = T("2027-09-20T23:59:59Z");
  EXPECT_EQ(f.not_after, na);
  EXPECT_EQ(lic::StateAt(f, T("2026-09-20T23:59:59Z")), lic::State::NotYetValid);
  EXPECT_EQ(lic::StateAt(f, na), lic::State::Valid);
  EXPECT_EQ(lic::StateAt(f, na + 1), lic::State::Expired);  // 유예 없음
  EXPECT_STREQ(lic::StateName(lic::StateAt(f, na + 1)), "EXPIRED");
}

// ---- 전체 판정(VerifyLicense) ----

Error Full(const std::string& text, int64_t now, lic::VerifiedLicense* v, lic::LicenseFields* f,
           std::string* detail = nullptr) {
  auto pub = PublicOf(TestSeed());
  std::string d;
  return lic::VerifyLicenseWith(text, pub.data(), now, v, f, detail ? detail : &d);
}

TEST(VerifyLicense, PeriodAndFieldsAreEnforced) {
  const std::string text = Issue(Contract::Dated());  // 2026-09-21 ~ 2027-09-20T23:59:59Z, 유예 없음
  lic::VerifiedLicense v;
  lic::LicenseFields f;
  std::string detail;
  EXPECT_EQ(Full(text, T("2026-09-20T23:59:59Z"), &v, &f, &detail), Error::NotYetValid);
  EXPECT_FALSE(v.valid());                                   // 실패하면 결과를 채우지 않음
  EXPECT_NE(detail.find("2026-09-21T00:00:00Z"), std::string::npos);
  EXPECT_EQ(Full(text, T("2027-09-20T23:59:59Z"), &v, &f), Error::Ok);   // 종료 순간까지 유효
  EXPECT_TRUE(v.valid());
  lic::VerifiedLicense v2;
  EXPECT_EQ(Full(text, T("2027-09-21T00:00:00Z"), &v2, nullptr, &detail), Error::Expired);  // 다음 1초에 만료
  EXPECT_FALSE(v2.valid());
  EXPECT_NE(detail.find("2027-09-20T23:59:59Z"), std::string::npos) << detail;
  EXPECT_EQ(Full(Replace(text, "\"sonastt\": 8", "\"sonastt\": 9"), T("2027-01-01T00:00:00Z"), &v2, nullptr),
            Error::BadSignature);                            // 서명 검증이 먼저
  const std::string missing = SignRaw(Replace(Contract{}.Json(), "\"site_id\":\"SEOUL-MAIN-DC\",", ""));
  EXPECT_EQ(Full(missing, T("2027-01-01T00:00:00Z"), &v2, nullptr), Error::InvalidField);
  // 운영(영구) 라이선스는 먼 미래에도 유효
  EXPECT_EQ(Full(Issue(Contract{}), T("9999-12-31T23:59:59Z"), &v2, nullptr), Error::Ok);
}

// 실패하면 출력을 건드리지 않고, 성공하면 서명 단계와 같은 서명 대상 바이트를 담는다.
TEST(VerifyLicense, OutputsOnlyOnSuccess) {
  auto pub = PublicOf(TestSeed());
  Contract old = Contract::Dated("2020-12-31T23:59:59Z");
  old.not_before = "2020-01-01T00:00:00Z";
  const std::string expired = Issue(old);
  const std::string valid = Issue(Contract{});
  const int64_t now = T("2027-01-01T00:00:00Z");
  lic::VerifiedLicense v;
  lic::LicenseFields f;
  f.site_id = "untouched";
  EXPECT_EQ(lic::VerifyLicenseWith(expired, pub.data(), now, &v, &f, nullptr), Error::Expired);
  EXPECT_FALSE(v.valid());
  EXPECT_EQ(f.site_id, "untouched");
  ASSERT_EQ(lic::VerifyLicenseWith(valid, pub.data(), now, &v, &f, nullptr), Error::Ok);
  EXPECT_EQ(f.site_id, "SEOUL-MAIN-DC");
  EXPECT_EQ(v.canonical_json(), VerifyOk(valid).canonical_json());
}

TEST(Utf16ToUtf8, ConversionAndRejection) {
  std::string out;
  ASSERT_TRUE(ToUtf8(U16("A한😀"), &out));
  EXPECT_EQ(out, "A\xED\x95\x9C\xF0\x9F\x98\x80");
  const std::uint16_t lone_high[] = {0x0041, 0xD83D};
  const std::uint16_t lone_low[] = {0xDE00, 0x0041};
  const std::uint16_t high_then_ascii[] = {0xD83D, 0x0041};
  EXPECT_FALSE(lic::Utf16ToUtf8(lone_high, 2, &out));
  EXPECT_FALSE(lic::Utf16ToUtf8(lone_low, 2, &out));
  EXPECT_FALSE(lic::Utf16ToUtf8(high_then_ascii, 2, &out));
  const std::uint16_t nul[] = {0x0000};
  ASSERT_TRUE(lic::Utf16ToUtf8(nul, 1, &out));
  EXPECT_EQ(out, std::string(1, '\0'));  // 표준 UTF-8 의 NUL 은 1바이트 (변형 UTF-8 은 C0 80)
  EXPECT_TRUE(lic::Utf16ToUtf8(nullptr, 0, &out));
  EXPECT_EQ(out, "");
}

// JNI 문자열 경로: UTF-16 → Utf16ToUtf8 은 원래 바이트와 같아 검증 성공.
// 반면 변형 UTF-8(GetStringUTFChars 방식, 보조 평면 문자를 서로게이트 3바이트×2 로 인코딩)은 실패한다.
TEST(Utf16ToUtf8, JniPathKeepsSignedBytesButModifiedUtf8Breaks) {
  Contract c;
  c.project_name = "STT 😀 한글";
  const std::string text = Issue(c);
  std::string back;
  ASSERT_TRUE(ToUtf8(U16(text), &back));
  EXPECT_EQ(back, text);
  lic::VerifiedLicense v;
  EXPECT_EQ(Verify(back, &v), Error::Ok);

  std::string modified = text;  // 😀 U+1F600 = D83D DE00 → CESU-8 ED A0 BD ED B8 80
  const std::string std_emoji = "\xF0\x9F\x98\x80", cesu_emoji = "\xED\xA0\xBD\xED\xB8\x80";
  modified.replace(modified.find(std_emoji), std_emoji.size(), cesu_emoji);
  EXPECT_NE(Verify(modified, &v), Error::Ok);
}

}  // namespace

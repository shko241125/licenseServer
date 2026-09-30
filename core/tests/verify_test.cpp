// 서명 전용 검증 API (VerifySignature / VerifiedLicense / ParseStandardFields / Utf16ToUtf8)
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

TEST(VerifySignature, ValuesComeOnlyFromSignedTree) {
  lic::VerifiedLicense v = VerifyOk(Issue(Contract{}));
  ASSERT_TRUE(v.valid());
  std::string s;
  std::int64_t n = 0;
  EXPECT_TRUE(v.GetString("site_id", &s));
  EXPECT_EQ(s, "SEOUL-MAIN-DC");
  EXPECT_TRUE(v.GetInt("allowed_channels.offline_stt", &n));
  EXPECT_EQ(n, 8);
  EXPECT_TRUE(v.GetString("validity.not_after", &s));
  EXPECT_EQ(s, "2027-09-20T23:59:59Z");
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

TEST(VerifySignature, CanonicalJsonMatchesGolden) {
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

// 공통 코어는 서명만 본다: 규격을 벗어난 값도 서명이 맞으면 Ok, 판정은 SDK 가 선택적으로 한다.
TEST(VerifySignature, SignatureOnlyAcceptsNonStandardValues) {
  const std::string odd = SignRaw(
      "{\"format_version\":7,\"allowed_channels\":{\"offline_stt\":999999},"
      "\"validity\":{\"not_after\":\"sometime\"},\"custom\":{\"legacy_mode\":true}}");
  lic::VerifiedLicense v = VerifyOk(odd);
  std::int64_t n = 0;
  bool b = false;
  EXPECT_TRUE(v.GetInt("allowed_channels.offline_stt", &n));
  EXPECT_EQ(n, 999999);
  EXPECT_TRUE(v.GetBool("custom.legacy_mode", &b));
  EXPECT_TRUE(b);
  lic::LicenseFields f;
  std::string detail;
  EXPECT_EQ(lic::ParseStandardFields(v, &f, &detail), Error::UnsupportedVersion) << detail;

  const std::string missing = SignRaw(Replace(Contract{}.Json(), "\"site_id\":\"SEOUL-MAIN-DC\",", ""));
  lic::VerifiedLicense v2 = VerifyOk(missing);
  EXPECT_EQ(lic::ParseStandardFields(v2, &f, &detail), Error::InvalidField);
  EXPECT_NE(detail.find("site_id"), std::string::npos);
}

TEST(VerifySignature, Rejections) {
  const std::string text = Issue(Contract{});
  lic::VerifiedLicense v;
  EXPECT_EQ(Verify(Replace(text, "\"offline_stt\": 8", "\"offline_stt\": 80"), &v), Error::BadSignature);
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

TEST(VerifySignature, EmptyObjectIsSafe) {
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

TEST(VerifySignature, StandardFieldsAndState) {
  lic::VerifiedLicense v = VerifyOk(Issue(Contract{}));
  lic::LicenseFields f;
  ASSERT_EQ(lic::ParseStandardFields(v, &f), Error::Ok);
  EXPECT_EQ(f.format_version, 1);
  EXPECT_EQ(f.site_id, "SEOUL-MAIN-DC");
  EXPECT_EQ(f.online_stt, 16);
  EXPECT_EQ(f.offline_stt, 8);
  EXPECT_EQ(f.grace_period_days, 14);
  const std::int64_t na = T("2027-09-20T23:59:59Z");
  EXPECT_EQ(f.not_after, na);
  EXPECT_EQ(lic::StateAt(f, T("2026-09-20T23:59:59Z")), lic::State::NotYetValid);
  EXPECT_EQ(lic::StateAt(f, na), lic::State::Valid);
  EXPECT_EQ(lic::StateAt(f, na + 1), lic::State::Grace);
  EXPECT_EQ(lic::StateAt(f, na + 14 * 86400 + 1), lic::State::Expired);
}

// ---- 값 검증 포함(VerifyLicense) / 모드 선택(Verify) ----

Error Full(const std::string& text, int64_t now, lic::VerifiedLicense* v, lic::LicenseFields* f,
           std::string* detail = nullptr) {
  auto pub = PublicOf(TestSeed());
  std::string d;
  return lic::VerifyLicenseWith(text, pub.data(), now, v, f, detail ? detail : &d);
}

TEST(VerifyLicense, PeriodAndFieldsAreEnforced) {
  const std::string text = Issue(Contract{});  // 2026-09-21 ~ 2027-09-20T23:59:59Z, 유예 14일
  lic::VerifiedLicense v;
  lic::LicenseFields f;
  std::string detail;
  EXPECT_EQ(Full(text, T("2026-09-20T23:59:59Z"), &v, &f, &detail), Error::NotYetValid);
  EXPECT_FALSE(v.valid());                                   // 실패하면 결과를 채우지 않음
  EXPECT_NE(detail.find("2026-09-21T00:00:00Z"), std::string::npos);
  EXPECT_EQ(Full(text, T("2027-10-04T23:59:59Z"), &v, &f), Error::Ok);   // 유예 마지막 순간
  EXPECT_TRUE(v.valid());
  EXPECT_EQ(lic::StateAt(f, T("2027-10-04T23:59:59Z")), lic::State::Grace);
  lic::VerifiedLicense v2;
  EXPECT_EQ(Full(text, T("2027-10-05T00:00:00Z"), &v2, nullptr, &detail), Error::Expired);
  EXPECT_FALSE(v2.valid());
  EXPECT_EQ(Full(Replace(text, "\"offline_stt\": 8", "\"offline_stt\": 9"), T("2027-01-01T00:00:00Z"), &v2, nullptr),
            Error::BadSignature);                            // 서명 검증이 먼저
  const std::string missing = SignRaw(Replace(Contract{}.Json(), "\"site_id\":\"SEOUL-MAIN-DC\",", ""));
  EXPECT_EQ(Full(missing, T("2027-01-01T00:00:00Z"), &v2, nullptr), Error::InvalidField);
}

// 하나의 진입점: 모드별 결과가 각각의 분리 함수와 정확히 같아야 한다.
TEST(VerifyMode, SingleEntryMatchesSeparateFunctions) {
  auto pub = PublicOf(TestSeed());
  Contract old;
  old.not_before = "2020-01-01T00:00:00Z";
  old.not_after = "2020-12-31T23:59:59Z";
  const std::string expired = Issue(old);
  const std::string nonstandard = SignRaw("{\"format_version\":7,\"allowed_channels\":{\"offline_stt\":999999}}");
  const std::string valid = Issue(Contract{});
  const int64_t now = T("2027-01-01T00:00:00Z");
  for (const std::string* text : {&valid, &expired, &nonstandard}) {
    for (lic::VerifyMode mode : {lic::VerifyMode::SignatureOnly, lic::VerifyMode::Full}) {
      lic::VerifiedLicense a, b;
      Error ea = lic::VerifyWith(*text, mode, pub.data(), now, &a, nullptr, nullptr);
      Error eb = mode == lic::VerifyMode::Full
                     ? lic::VerifyLicenseWith(*text, pub.data(), now, &b, nullptr, nullptr)
                     : lic::VerifySignatureWith(*text, pub.data(), &b, nullptr);
      EXPECT_EQ(ea, eb) << lic::VerifyModeName(mode);
      EXPECT_EQ(a.canonical_json(), b.canonical_json());
    }
  }
  // 서명만: 만료·비표준 라이선스도 통과 / 전체: 거부 — SDK 가 상황에 따라 고른다
  lic::VerifiedLicense v;
  EXPECT_EQ(lic::VerifyWith(expired, lic::VerifyMode::SignatureOnly, pub.data(), now, &v, nullptr, nullptr), Error::Ok);
  EXPECT_EQ(lic::VerifyWith(expired, lic::VerifyMode::Full, pub.data(), now, &v, nullptr, nullptr), Error::Expired);
  EXPECT_EQ(lic::VerifyWith(nonstandard, lic::VerifyMode::SignatureOnly, pub.data(), now, &v, nullptr, nullptr), Error::Ok);
  EXPECT_EQ(lic::VerifyWith(nonstandard, lic::VerifyMode::Full, pub.data(), now, &v, nullptr, nullptr),
            Error::UnsupportedVersion);
  // SignatureOnly 는 fields 를 건드리지 않는다
  lic::LicenseFields f;
  f.site_id = "untouched";
  EXPECT_EQ(lic::VerifyWith(valid, lic::VerifyMode::SignatureOnly, pub.data(), now, &v, &f, nullptr), Error::Ok);
  EXPECT_EQ(f.site_id, "untouched");
  EXPECT_EQ(lic::VerifyWith(valid, lic::VerifyMode::Full, pub.data(), now, &v, &f, nullptr), Error::Ok);
  EXPECT_EQ(f.site_id, "SEOUL-MAIN-DC");
}

TEST(VerifyMode, ParseFromConfigString) {
  lic::VerifyMode m = lic::VerifyMode::Full;
  EXPECT_TRUE(lic::ParseVerifyMode("signature", &m));
  EXPECT_EQ(m, lic::VerifyMode::SignatureOnly);
  EXPECT_TRUE(lic::ParseVerifyMode("full", &m));
  EXPECT_EQ(m, lic::VerifyMode::Full);
  EXPECT_FALSE(lic::ParseVerifyMode("FULL", &m));   // 오타·대소문자는 거부 → SDK 가 기본값/오류를 명시적으로 결정
  EXPECT_FALSE(lic::ParseVerifyMode("", &m));
  EXPECT_STREQ(lic::VerifyModeName(lic::VerifyMode::SignatureOnly), "signature");
  EXPECT_STREQ(lic::VerifyModeName(lic::VerifyMode::Full), "full");
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

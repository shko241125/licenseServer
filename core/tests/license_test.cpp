#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "test_util.h"

namespace {

using namespace testutil;
using lic::Error;
using lic::State;

std::vector<uint8_t> Hex(const std::string& h) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i + 1 < h.size(); i += 2) {
    out.push_back(static_cast<uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16)));
  }
  return out;
}

Error Verify(const std::string& text, lic::LicenseData* out = nullptr,
             const std::array<uint8_t, 32>& seed = TestSeed()) {
  lic::LicenseData d;
  std::string detail;
  auto pub = PublicOf(seed);
  Error e = lic::ParseAndVerify(text, pub.data(), out ? out : &d, &detail);
  return e;
}

// ---------- 암호 기본 ----------

// RFC 8032 §7.1 TEST 1 (빈 메시지)
TEST(Crypto, Rfc8032Vector1) {
  auto seed = Hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
  auto pub_expected = Hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
  auto sig_expected = Hex(
      "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e3970"
      "1cf9b46bd25bf5f0595bbe24655141438e7a100b");
  uint8_t pub[32], sig[64];
  ASSERT_TRUE(crypto::Ed25519PublicFromSeed(seed.data(), pub));
  EXPECT_EQ(std::vector<uint8_t>(pub, pub + 32), pub_expected);
  ASSERT_TRUE(crypto::Ed25519Sign(seed.data(), "", sig));
  EXPECT_EQ(std::vector<uint8_t>(sig, sig + 64), sig_expected);
  EXPECT_TRUE(crypto::Ed25519Verify(pub, "", sig));
  sig[0] ^= 1;
  EXPECT_FALSE(crypto::Ed25519Verify(pub, "", sig));
}

TEST(Crypto, Base64RoundTripAndStrictness) {
  const uint8_t data[] = {0xff, 0x00, 0x10, 0x80, 0x7f};
  for (size_t n = 0; n <= sizeof data; ++n) {
    std::vector<uint8_t> out;
    std::string enc = crypto::Base64Encode(data, n);
    ASSERT_TRUE(crypto::Base64Decode(enc, &out)) << enc;
    EXPECT_EQ(out, std::vector<uint8_t>(data, data + n));
  }
  EXPECT_EQ(crypto::Base64Encode(reinterpret_cast<const uint8_t*>("foobar"), 6), "Zm9vYmFy");
  std::vector<uint8_t> out;
  for (const char* bad : {"Zg=", "Zh==", "Zm9=", "Zm8", "Z===", "Zg= =", "Zg==\n", " Zg==",
                          "Zg==Zg==", "Zm9v YmFy", "Zm9v-mFy", "Zm9v_mFy", "=Zg="}) {
    EXPECT_FALSE(crypto::Base64Decode(bad, &out)) << bad;
  }
}

// ---------- 시간 ----------

TEST(Time, ParseAndFormat) {
  int64_t t;
  ASSERT_TRUE(lic::ParseUtcTime("1970-01-01T00:00:00Z", &t));
  EXPECT_EQ(t, 0);
  ASSERT_TRUE(lic::ParseUtcTime("2026-09-21T00:00:00Z", &t));
  EXPECT_EQ(t, 1789948800);  // python calendar.timegm 으로 교차 확인
  ASSERT_TRUE(lic::ParseUtcTime("2038-01-19T03:14:08Z", &t));
  EXPECT_EQ(t, 2147483648LL);  // 32비트 time_t 경계 너머
  EXPECT_TRUE(lic::ParseUtcTime("2024-02-29T23:59:59Z", &t));
  EXPECT_TRUE(lic::ParseUtcTime("2000-02-29T00:00:00Z", &t));
  for (const char* bad : {"2023-02-29T00:00:00Z", "1900-02-29T00:00:00Z", "2026-13-01T00:00:00Z",
                          "2026-00-01T00:00:00Z", "2026-04-31T00:00:00Z", "2026-01-01T24:00:00Z",
                          "2026-01-01T00:60:00Z", "2026-01-01T00:00:60Z", "2026-01-01T00:00:00",
                          "2026-01-01T00:00:00z", "2026-01-01 00:00:00Z", "2026-01-01T00:00:00+09:00",
                          "2026-01-01T00:00:00.000Z", "1969-12-31T23:59:59Z", "26-01-01T00:00:00Z",
                          "2026-1-01T00:00:00Z", "２026-01-01T00:00:00Z", ""}) {
    EXPECT_FALSE(lic::ParseUtcTime(bad, &t)) << bad;
  }
  for (int64_t v : {int64_t{0}, int64_t{1789948800}, int64_t{951782399}, int64_t{4102444799},
                    int64_t{253402300799}}) {
    int64_t back;
    ASSERT_TRUE(lic::ParseUtcTime(lic::FormatUtcTime(v), &back)) << lic::FormatUtcTime(v);
    EXPECT_EQ(back, v);
  }
  EXPECT_EQ(lic::FormatUtcTime(253402300799), "9999-12-31T23:59:59Z");
}

// ---------- 발급 → 검증 ----------

TEST(License, IssueAndVerifyRoundTrip) {
  Contract c;
  std::string text = Issue(c);
  lic::LicenseData d;
  ASSERT_EQ(Verify(text, &d), Error::Ok);
  EXPECT_EQ(d.format_version, 1);
  EXPECT_EQ(d.license_id, c.license_id);
  EXPECT_EQ(d.project_name, c.project_name);
  EXPECT_EQ(d.license_type, "production");
  EXPECT_EQ(d.site_id, "SEOUL-MAIN-DC");
  EXPECT_EQ(d.online, 16);
  EXPECT_EQ(d.offline, 8);
  EXPECT_EQ(d.not_before, T("2026-09-21T00:00:00Z"));
  EXPECT_EQ(d.not_after, T("2027-09-20T23:59:59Z"));
  EXPECT_EQ(d.grace_days, 14);
  EXPECT_EQ(d.warning_notice, c.warning_notice);
  // 사람이 읽을 수 있는 형태: 원문 §3 순서, signature 마지막
  EXPECT_EQ(text.rfind("{\n  \"format_version\": 1,\n  \"license_id\"", 0), 0u) << text;
  EXPECT_NE(text.find("  \"allowed_channels\": {\n    \"online_stt\": 16,\n    \"offline_stt\": 8\n  },"),
            std::string::npos);
  EXPECT_NE(text.find(",\n  \"signature\": \""), std::string::npos);
  EXPECT_EQ(text.back(), '\n');
}

TEST(License, DetectsTampering) {
  const std::string text = Issue(Contract{});
  const std::vector<std::pair<std::string, std::string>> edits = {
      {"\"online_stt\": 16", "\"online_stt\": 17"},
      {"\"offline_stt\": 8", "\"offline_stt\": 16"},
      {"2027-09-20T23:59:59Z", "2028-09-20T23:59:59Z"},
      {"\"grace_period_days\": 14", "\"grace_period_days\": 90"},
      {"SEOUL-MAIN-DC", "BUSAN-DC"},
      {"STT Platform", "STT PlatforM"},
      {"strictly prohibited.", "strictly prohibited!"},
      {"\"production\"", "\"trial\""},
      {"\"format_version\": 1,", "\"format_version\": 1, \"extra\": 1,"},  // 필드 추가
      {"  \"site_id\": \"SEOUL-MAIN-DC\",\n", ""},                         // 필드 삭제
  };
  for (const auto& [from, to] : edits) {
    Error e = Verify(Replace(text, from, to));
    EXPECT_EQ(e, Error::BadSignature) << from << " -> " << to << " : " << lic::ErrorName(e);
  }
}

TEST(License, FormattingDoesNotMatter) {
  const std::string text = Issue(Contract{});
  lic::json::Value v;
  std::string err;
  ASSERT_TRUE(lic::json::Parse(text, &v, &err));
  // 키 순서 뒤섞기 + 공백 제거
  EXPECT_EQ(Verify(lic::json::Canonicalize(v)), Error::Ok);
  EXPECT_EQ(Verify(lic::json::Pretty(v, {"signature", "validity", "site_id"})), Error::Ok);
  // CRLF, BOM, 앞뒤 공백
  std::string crlf;
  for (char ch : text) {
    if (ch == '\n') crlf += "\r\n"; else crlf += ch;
  }
  EXPECT_EQ(Verify(crlf), Error::Ok);
  EXPECT_EQ(Verify("\xEF\xBB\xBF" + text), Error::Ok);
  EXPECT_EQ(Verify("\n\t  " + text + "  \n\n"), Error::Ok);
  // 동일 의미의 \u 이스케이프 표기
  EXPECT_EQ(Verify(Replace(text, "SEOUL-MAIN-DC", "\\u0053EOUL-MAIN-DC")), Error::Ok);
}

TEST(License, SignatureFormat) {
  const std::string text = Issue(Contract{});
  lic::json::Value v;
  std::string err;
  ASSERT_TRUE(lic::json::Parse(text, &v, &err));
  const std::string sig = v.Find("signature")->str;
  ASSERT_EQ(sig.size(), 88u);

  EXPECT_EQ(Verify(text, nullptr, TestSeed(100)), Error::BadSignature);  // 다른 키
  EXPECT_EQ(Verify(Replace(text, sig, sig.substr(0, 84) + "AA==")), Error::BadSignature);
  EXPECT_EQ(Verify(Replace(text, sig, sig.substr(0, 86))), Error::BadSignature);        // 길이
  EXPECT_EQ(Verify(Replace(text, sig, crypto::Base64Encode(
                                          reinterpret_cast<const uint8_t*>(sig.data()), 63))),
            Error::BadSignature);
  EXPECT_EQ(Verify(Replace(text, sig, sig + "AAAA")), Error::BadSignature);
  EXPECT_EQ(Verify(Replace(text, sig, " " + sig)), Error::BadSignature);
  EXPECT_EQ(Verify(Replace(text, "\"" + sig + "\"", "12")), Error::BadSignature);
  EXPECT_EQ(Verify(Replace(text, "\"" + sig + "\"", "true")), Error::BadSignature);
  v.Erase("signature");
  EXPECT_EQ(Verify(lic::json::Canonicalize(v)), Error::BadSignature);
}

TEST(License, StrictParsing) {
  const std::string text = Issue(Contract{});
  EXPECT_EQ(Verify(Replace(text, "\"online_stt\": 16", "\"online_stt\": 16, \"online_stt\": 16")),
            Error::ParseError);
  EXPECT_EQ(Verify(Replace(text, "\"online_stt\": 16", "\"online_stt\": 16.0")), Error::ParseError);
  EXPECT_EQ(Verify(Replace(text, "\"online_stt\": 16", "\"online_stt\": -16")), Error::ParseError);
  EXPECT_EQ(Verify(text + "{}"), Error::ParseError);
  EXPECT_EQ(Verify("[]"), Error::ParseError);
  EXPECT_EQ(Verify("\"x\""), Error::ParseError);
  EXPECT_EQ(Verify(""), Error::ParseError);
  EXPECT_EQ(Verify(std::string(lic::kMaxFileSize + 1, ' ')), Error::FileTooLarge);
}

TEST(License, SchemaValidationAfterSignature) {
  Contract c;
  auto signed_json = [&](const std::string& from, const std::string& to) {
    return SignRaw(Replace(c.Json(), from, to));
  };
  // 서명은 맞지만 내용이 규격 위반
  EXPECT_EQ(Verify(signed_json("\"format_version\":1", "\"format_version\":2")),
            Error::UnsupportedVersion);
  EXPECT_EQ(Verify(signed_json("\"format_version\":1,", "")), Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("\"format_version\":1", "\"format_version\":\"1\"")),
            Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("\"site_id\":\"SEOUL-MAIN-DC\",", "")), Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("\"site_id\":\"SEOUL-MAIN-DC\"", "\"site_id\":\"\"")),
            Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("SEOUL-MAIN-DC", "SEOUL\\nMAIN")), Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("\"online_stt\":16", "\"online_stt\":\"16\"")), Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("\"online_stt\":16", "\"online_stt\":100001")), Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("\"online_stt\":16,\"offline_stt\":8",
                               "\"online_stt\":0,\"offline_stt\":0")),
            Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("\"grace_period_days\":14", "\"grace_period_days\":91")),
            Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("2027-09-20T23:59:59Z", "2027-09-20T23:59:59+09:00")),
            Error::InvalidField);
  EXPECT_EQ(Verify(signed_json("2027-09-20T23:59:59Z", "2026-09-21T00:00:00Z")),
            Error::InvalidField);  // not_before == not_after
  EXPECT_EQ(Verify(signed_json("\"allowed_channels\":{", "\"allowed_channels\":true,\"x\":{")),
            Error::InvalidField);
  // 모르는 필드는 서명에 포함되어 있으면 허용 (신규 발급기 ↔ 구버전 SDK 호환)
  EXPECT_EQ(Verify(signed_json("\"format_version\":1,", "\"format_version\":1,\"future_field\":{\"x\":1},")),
            Error::Ok);
  // 한 채널 종류만 0 인 것은 허용
  EXPECT_EQ(Verify(signed_json("\"online_stt\":16", "\"online_stt\":0")), Error::Ok);
}

TEST(License, StateBoundaries) {
  lic::LicenseData d;
  ASSERT_EQ(Verify(Issue(Contract{}), &d), Error::Ok);
  const int64_t nb = T("2026-09-21T00:00:00Z"), na = T("2027-09-20T23:59:59Z");
  EXPECT_EQ(d.StateAt(nb - 1), State::NotYetValid);
  EXPECT_EQ(d.StateAt(nb), State::Valid);
  EXPECT_EQ(d.StateAt(na), State::Valid);
  EXPECT_EQ(d.StateAt(na + 1), State::Grace);
  EXPECT_EQ(d.StateAt(na + 14 * 86400), State::Grace);
  EXPECT_EQ(d.StateAt(na + 14 * 86400 + 1), State::Expired);

  Contract c0;
  c0.grace = 0;
  ASSERT_EQ(Verify(Issue(c0), &d), Error::Ok);
  EXPECT_EQ(d.StateAt(na), State::Valid);
  EXPECT_EQ(d.StateAt(na + 1), State::Expired);
}

TEST(License, SignContractDefaultsAndChecks) {
  auto seed = TestSeed();
  std::string text, detail;
  lic::LicenseData d;
  // format_version, license_id, issued_at 생략 → 채워짐
  std::string minimal = Contract{}.Json();
  minimal = Replace(minimal, "\"format_version\":1,", "");
  minimal = Replace(minimal, "\"license_id\":\"0123456789abcdef0123456789abcdef\",", "");
  minimal = Replace(minimal, "\"issued_at\":\"2026-09-21T00:00:00Z\",", "");
  const int64_t now = T("2026-09-20T12:34:56Z");
  ASSERT_EQ(lic::SignContract(minimal, seed.data(), now, &text, &d, &detail), Error::Ok) << detail;
  EXPECT_EQ(d.format_version, 1);
  EXPECT_EQ(d.license_id.size(), 32u);
  EXPECT_EQ(d.license_id.find_first_not_of("0123456789abcdef"), std::string::npos);
  EXPECT_EQ(d.issued_at, now);

  std::string text2;
  lic::LicenseData d2;
  ASSERT_EQ(lic::SignContract(minimal, seed.data(), now, &text2, &d2, &detail), Error::Ok);
  EXPECT_NE(d.license_id, d2.license_id);  // 발급마다 새 ID

  Contract bad_type;
  bad_type.license_type = "enterprise";
  EXPECT_EQ(lic::SignContract(bad_type.Json(), seed.data(), now, &text, &d, &detail),
            Error::InvalidField);
  const std::string with_sig = Replace(Contract{}.Json(), "\"format_version\":1",
                                       "\"format_version\":1,\"signature\":\"x\"");
  EXPECT_EQ(lic::SignContract(with_sig, seed.data(), now, &text, &d, &detail), Error::InvalidField);
  EXPECT_EQ(lic::SignContract("{", seed.data(), now, &text, &d, &detail), Error::ParseError);
  Contract inverted;
  inverted.not_after = "2026-01-01T00:00:00Z";
  EXPECT_EQ(lic::SignContract(inverted.Json(), seed.data(), now, &text, &d, &detail),
            Error::InvalidField);
}

// ---------- 골든 벡터 ----------
// 고정 seed + 고정 계약 → 바이트 단위로 고정된 license.lic.
// 정규화·출력 규칙이 바뀌면 이 테스트가 실패한다(= 이미 배포된 라이선스가 깨질 수 있다는 경보).
// 재생성: STT_UPDATE_GOLDEN=1 ./stt_license_tests --gtest_filter=Golden.*
TEST(Golden, LicenseFileIsStable) {
  const std::string dir = STT_TESTDATA_DIR;
  const auto seed = TestSeed();
  const auto pub = PublicOf(seed);
  const std::string text = Issue(Contract{}, seed);
  lic::json::Value v;
  std::string err;
  ASSERT_TRUE(lic::json::Parse(text, &v, &err));
  v.Erase("signature");
  const std::string canonical = lic::json::Canonicalize(v);
  const std::string pub_b64 = crypto::Base64Encode(pub.data(), pub.size()) + "\n";

  if (std::getenv("STT_UPDATE_GOLDEN")) {
    std::ofstream(dir + "/golden_license.lic", std::ios::binary) << text;
    std::ofstream(dir + "/golden_canonical.json", std::ios::binary) << canonical;
    std::ofstream(dir + "/golden_public.key", std::ios::binary) << pub_b64;
    GTEST_SKIP() << "golden files updated";
  }
  auto read = [&](const std::string& name) {
    std::ifstream f(dir + "/" + name, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
  };
  EXPECT_EQ(read("golden_license.lic"), text);
  EXPECT_EQ(read("golden_canonical.json"), canonical);
  EXPECT_EQ(read("golden_public.key"), pub_b64);
  EXPECT_EQ(Verify(read("golden_license.lic")), Error::Ok);
}

}  // namespace

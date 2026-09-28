#include <gtest/gtest.h>

#include "json.h"

namespace json = stt::license::json;

namespace {

std::string Canon(const std::string& in) {
  json::Value v;
  std::string err;
  EXPECT_TRUE(json::Parse(in, &v, &err)) << in << " -> " << err;
  return json::Canonicalize(v);
}

bool Rejects(const std::string& in) {
  json::Value v;
  std::string err;
  return !json::Parse(in, &v, &err) && !err.empty();
}

TEST(JsonCanonical, SortsKeysAndRemovesWhitespace) {
  EXPECT_EQ(Canon("{ \"b\" : 1 ,\n\t\"a\" : { \"d\" : true , \"c\" : \"x\" } }\r\n"),
            "{\"a\":{\"c\":\"x\",\"d\":true},\"b\":1}");
  EXPECT_EQ(Canon("{}"), "{}");
  EXPECT_EQ(Canon("{\"a\":{}}"), "{\"a\":{}}");
  EXPECT_EQ(Canon("{\"a_1\":0,\"a\":false,\"a0\":9007199254740991}"),
            "{\"a\":false,\"a0\":9007199254740991,\"a_1\":0}");
}

// RFC 8785 §3.2.2.2 의 문자열 예시
TEST(JsonCanonical, Rfc8785StringExample) {
  const std::string in =
      "{\"k\":\"\\u20ac$\\u000F\\u000aA'\\u0042\\u0022\\u005c\\\\\\\"\\/\"}";
  EXPECT_EQ(Canon(in), "{\"k\":\"\xE2\x82\xAC$\\u000f\\nA'B\\\"\\\\\\\\\\\"/\"}");
}

TEST(JsonCanonical, StringEscaping) {
  // 한글·<>&·U+2028·DEL 은 그대로, 제어문자는 소문자 \u00xx 또는 단축형
  EXPECT_EQ(Canon("{\"k\":\"한글 <>& \xE2\x80\xA8 \x7F\"}"), "{\"k\":\"한글 <>& \xE2\x80\xA8 \x7F\"}");
  EXPECT_EQ(Canon("{\"k\":\"\\u0001\\u001F\\b\\f\\n\\r\\t\\u0000\"}"),
            "{\"k\":\"\\u0001\\u001f\\b\\f\\n\\r\\t\\u0000\"}");
  // 서로게이트 쌍 → UTF-8 4바이트
  EXPECT_EQ(Canon("{\"k\":\"\\ud83d\\ude00\"}"), "{\"k\":\"\xF0\x9F\x98\x80\"}");
  // \u 로 표기된 한글도 원문 UTF-8 로 정규화
  EXPECT_EQ(Canon("{\"k\":\"\\ud55c\"}"), "{\"k\":\"한\"}");
}

TEST(JsonCanonical, Idempotent) {
  const std::string c = Canon("{\"z\":{\"y\":\"\\u0007\\\"\",\"x\":12},\"a\":\"한\"}");
  EXPECT_EQ(Canon(c), c);
}

TEST(JsonParse, RejectsUnsupportedTypes) {
  EXPECT_TRUE(Rejects("{\"a\":null}"));
  EXPECT_TRUE(Rejects("{\"a\":[1]}"));
  EXPECT_TRUE(Rejects("{\"a\":-1}"));
  EXPECT_TRUE(Rejects("{\"a\":1.0}"));
  EXPECT_TRUE(Rejects("{\"a\":16.0}"));
  EXPECT_TRUE(Rejects("{\"a\":1e2}"));
  EXPECT_TRUE(Rejects("{\"a\":1E2}"));
  EXPECT_TRUE(Rejects("{\"a\":01}"));
  EXPECT_TRUE(Rejects("{\"a\":9007199254740992}"));
  EXPECT_TRUE(Rejects("{\"a\":99999999999999999999999}"));
  EXPECT_TRUE(Rejects("{\"a\":tru}"));
  EXPECT_TRUE(Rejects("{\"a\":True}"));
}

TEST(JsonParse, RejectsStructuralErrors) {
  EXPECT_TRUE(Rejects(""));
  EXPECT_TRUE(Rejects("   "));
  EXPECT_TRUE(Rejects("{"));
  EXPECT_TRUE(Rejects("{\"a\":1,}"));
  EXPECT_TRUE(Rejects("{,\"a\":1}"));
  EXPECT_TRUE(Rejects("{\"a\" 1}"));
  EXPECT_TRUE(Rejects("{\"a\":1}{}"));
  EXPECT_TRUE(Rejects("{\"a\":1} x"));
  EXPECT_TRUE(Rejects("{\"a\":1,\"a\":2}"));        // 중복 키
  EXPECT_TRUE(Rejects("{\"a\":1,\"b\":{},\"a\":1}"));
  EXPECT_TRUE(Rejects("{\"A\":1}"));                // 키 문자 제한
  EXPECT_TRUE(Rejects("{\"a-b\":1}"));
  EXPECT_TRUE(Rejects("{\"\":1}"));
  EXPECT_TRUE(Rejects("{\"" + std::string(65, 'a') + "\":1}"));
  EXPECT_TRUE(Rejects("{a:1}"));
  EXPECT_TRUE(Rejects("{'a':1}"));
  EXPECT_TRUE(Rejects("{\"a\":1 /*c*/}"));
}

TEST(JsonParse, DepthLimit) {
  std::string ok, bad;
  for (int i = 0; i < json::kMaxDepth; ++i) ok += "{\"a\":";
  ok += "1" + std::string(json::kMaxDepth, '}');
  EXPECT_FALSE(Rejects(ok));
  for (int i = 0; i <= json::kMaxDepth; ++i) bad += "{\"a\":";
  bad += "1" + std::string(json::kMaxDepth + 1, '}');
  EXPECT_TRUE(Rejects(bad));
}

TEST(JsonParse, RejectsBadStrings) {
  EXPECT_TRUE(Rejects("{\"a\":\"abc}"));                 // 미종결
  EXPECT_TRUE(Rejects("{\"a\":\"a\nb\"}"));              // 이스케이프 안 된 제어문자
  EXPECT_TRUE(Rejects("{\"a\":\"\\x41\"}"));             // 잘못된 이스케이프
  EXPECT_TRUE(Rejects("{\"a\":\"\\u12\"}"));             // 짧은 \u
  EXPECT_TRUE(Rejects("{\"a\":\"\\u12G4\"}"));
  EXPECT_TRUE(Rejects("{\"a\":\"\\ud800\"}"));           // 짝 없는 high surrogate
  EXPECT_TRUE(Rejects("{\"a\":\"\\udc00\"}"));           // 짝 없는 low surrogate
  EXPECT_TRUE(Rejects("{\"a\":\"\\ud800\\u0041\"}"));
  EXPECT_TRUE(Rejects("{\"a\":\"\xC0\xAF\"}"));          // overlong
  EXPECT_TRUE(Rejects("{\"a\":\"\xE0\x80\xAF\"}"));      // overlong 3바이트
  EXPECT_TRUE(Rejects("{\"a\":\"\xED\xA0\x80\"}"));      // UTF-8 로 인코딩된 surrogate
  EXPECT_TRUE(Rejects("{\"a\":\"\xF4\x90\x80\x80\"}"));  // U+10FFFF 초과
  EXPECT_TRUE(Rejects("{\"a\":\"\xF5\x80\x80\x80\"}"));
  EXPECT_TRUE(Rejects("{\"a\":\"\x80\"}"));              // 단독 continuation
  EXPECT_TRUE(Rejects("{\"a\":\"\xE2\x82\"}"));          // 잘린 시퀀스
  EXPECT_TRUE(Rejects("{\"a\":\"\xFF\"}"));
}

TEST(JsonParse, AcceptsBoundaryValues) {
  EXPECT_FALSE(Rejects("{\"a\":0}"));
  EXPECT_FALSE(Rejects("{\"a\":9007199254740991}"));
  EXPECT_FALSE(Rejects("{\"a\":\"\xF4\x8F\xBF\xBF\"}"));  // U+10FFFF
  EXPECT_FALSE(Rejects("{\"a\":\"\\/\"}"));
  EXPECT_FALSE(Rejects("\"top-level string\""));         // 파서 자체는 허용, 라이선스 계층에서 거부
}

TEST(JsonValue, SetFindErase) {
  json::Value v;
  v.Set("b", json::MakeInt(2));
  v.Set("a", json::MakeInt(1));
  v.Set("c", json::MakeInt(3));
  v.Set("b", json::MakeInt(20));
  EXPECT_EQ(json::Canonicalize(v), "{\"a\":1,\"b\":20,\"c\":3}");
  ASSERT_NE(v.Find("c"), nullptr);
  EXPECT_EQ(v.Find("c")->num, 3);
  EXPECT_EQ(v.Find("zz"), nullptr);
  v.Erase("b");
  v.Erase("nope");
  EXPECT_EQ(json::Canonicalize(v), "{\"a\":1,\"c\":3}");
}

TEST(JsonPretty, RankOrderThenAlphabetical) {
  json::Value v;
  std::string err;
  ASSERT_TRUE(json::Parse("{\"z\":1,\"b\":{\"y\":2,\"x\":1},\"a\":\"s\",\"m\":{}}", &v, &err));
  EXPECT_EQ(json::Pretty(v, {"b", "a"}),
            "{\n  \"b\": {\n    \"x\": 1,\n    \"y\": 2\n  },\n  \"a\": \"s\",\n  \"m\": {},\n"
            "  \"z\": 1\n}");
}

}  // namespace

// 라이선스 스키마 전용 JSON 부분집합: 엄격 파서 + RFC 8785 정규화 + 사람이 읽는 출력.
// 지원: object, string, 0 이상 2^53-1 이하 정수, true/false. 그 외(null, array, 음수, 실수, 지수)는 거부.
// 객체 키는 [a-z0-9_]{1,64} 로 제한한다 → 바이트 순 정렬이 RFC 8785의 UTF-16 코드 단위 정렬과 같다.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace stt::license::json {

struct Value {
  enum class Type { Object, String, Int, Bool };
  Type type = Type::Object;
  std::vector<std::pair<std::string, Value>> members;  // Object: 키 오름차순, 중복 없음
  std::string str;                                      // String: UTF-8 (검증됨)
  int64_t num = 0;                                      // Int
  bool boolean = false;                                 // Bool

  const Value* Find(std::string_view key) const;
  void Erase(std::string_view key);
  void Set(std::string key, Value v);  // 정렬 유지
};

Value MakeString(std::string s);
Value MakeInt(int64_t n);

// 성공 시 true. 실패 시 err에 사람이 읽을 사유(바이트 위치 포함).
bool Parse(std::string_view in, Value* out, std::string* err);

// RFC 8785 정규 직렬화 (공백 없음, 키 정렬).
std::string Canonicalize(const Value& v);

// 2칸 들여쓰기 출력. key_rank에 있는 키는 그 순서대로, 없는 키는 그 뒤에 사전순.
std::string Pretty(const Value& v, const std::vector<std::string>& key_rank);

constexpr int64_t kMaxSafeInt = 9007199254740991;  // 2^53 - 1
constexpr int kMaxDepth = 16;

}  // namespace stt::license::json

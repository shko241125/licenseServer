#include "json.h"

#include <algorithm>

namespace stt::license::json {
namespace {

bool KeyLess(const std::pair<std::string, Value>& a, const std::pair<std::string, Value>& b) {
  return a.first < b.first;
}

bool ValidKey(std::string_view k) {
  if (k.empty() || k.size() > 64) return false;
  for (char c : k) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
  }
  return true;
}

void AppendUtf8(uint32_t cp, std::string* out) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

class Parser {
 public:
  Parser(std::string_view s, std::string* err) : s_(s), err_(err) {}

  bool ParseDocument(Value* out) {
    SkipWs();
    if (!ParseValue(out, 0)) return false;
    SkipWs();
    if (i_ != s_.size()) return Fail("trailing data after JSON value");
    return true;
  }

 private:
  bool Fail(const char* msg) {
    if (err_) *err_ = std::string(msg) + " at byte " + std::to_string(i_);
    return false;
  }
  bool AtEnd() const { return i_ >= s_.size(); }
  unsigned char Peek() const { return static_cast<unsigned char>(s_[i_]); }

  void SkipWs() {
    while (!AtEnd() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
  }

  bool Literal(std::string_view word) {
    if (s_.substr(i_, word.size()) != word) return Fail("invalid literal");
    i_ += word.size();
    return true;
  }

  bool ParseValue(Value* out, int depth) {
    if (AtEnd()) return Fail("unexpected end of input");
    switch (Peek()) {
      case '{': return ParseObject(out, depth);
      case '"': out->type = Value::Type::String; return ParseString(&out->str);
      case 't': out->type = Value::Type::Bool; out->boolean = true; return Literal("true");
      case 'f': out->type = Value::Type::Bool; out->boolean = false; return Literal("false");
      case 'n': return Fail("null is not supported");
      case '[': return Fail("arrays are not supported");
      case '-': return Fail("negative numbers are not supported");
      default:
        if (Peek() >= '0' && Peek() <= '9') return ParseInt(out);
        return Fail("unexpected character");
    }
  }

  bool ParseObject(Value* out, int depth) {
    if (depth >= kMaxDepth) return Fail("nesting too deep");
    out->type = Value::Type::Object;
    out->members.clear();
    ++i_;  // '{'
    SkipWs();
    if (!AtEnd() && Peek() == '}') { ++i_; return true; }
    for (;;) {
      SkipWs();
      if (AtEnd() || Peek() != '"') return Fail("expected object key");
      std::string key;
      if (!ParseString(&key)) return false;
      if (!ValidKey(key)) return Fail("object key must match [a-z0-9_]{1,64}");
      SkipWs();
      if (AtEnd() || Peek() != ':') return Fail("expected ':'");
      ++i_;
      SkipWs();
      Value v;
      if (!ParseValue(&v, depth + 1)) return false;
      out->members.emplace_back(std::move(key), std::move(v));
      SkipWs();
      if (AtEnd()) return Fail("unexpected end of input in object");
      if (Peek() == ',') { ++i_; continue; }
      if (Peek() == '}') { ++i_; break; }
      return Fail("expected ',' or '}'");
    }
    std::sort(out->members.begin(), out->members.end(), KeyLess);
    for (size_t k = 1; k < out->members.size(); ++k) {
      if (out->members[k - 1].first == out->members[k].first) {
        if (err_) *err_ = "duplicate key \"" + out->members[k].first + "\"";
        return false;
      }
    }
    return true;
  }

  bool ParseInt(Value* out) {
    out->type = Value::Type::Int;
    int64_t n = 0;
    if (Peek() == '0') {
      ++i_;
    } else {
      while (!AtEnd() && Peek() >= '0' && Peek() <= '9') {
        n = n * 10 + (Peek() - '0');
        if (n > kMaxSafeInt) return Fail("integer exceeds 2^53-1");
        ++i_;
      }
    }
    if (!AtEnd() && (Peek() == '.' || Peek() == 'e' || Peek() == 'E')) {
      return Fail("only integers are supported");
    }
    if (!AtEnd() && Peek() >= '0' && Peek() <= '9') return Fail("leading zero");
    out->num = n;
    return true;
  }

  bool Hex4(uint32_t* out) {
    if (s_.size() - i_ < 4) return Fail("truncated \\u escape");
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      char c = s_[i_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return Fail("invalid hex digit in \\u escape");
    }
    *out = v;
    return true;
  }

  bool ParseEscape(std::string* out) {
    ++i_;  // '\'
    if (AtEnd()) return Fail("unexpected end of input in escape");
    char c = s_[i_++];
    switch (c) {
      case '"': out->push_back('"'); return true;
      case '\\': out->push_back('\\'); return true;
      case '/': out->push_back('/'); return true;
      case 'b': out->push_back('\b'); return true;
      case 'f': out->push_back('\f'); return true;
      case 'n': out->push_back('\n'); return true;
      case 'r': out->push_back('\r'); return true;
      case 't': out->push_back('\t'); return true;
      case 'u': {
        uint32_t cp = 0;
        if (!Hex4(&cp)) return false;
        if (cp >= 0xDC00 && cp <= 0xDFFF) return Fail("unpaired low surrogate");
        if (cp >= 0xD800 && cp <= 0xDBFF) {
          if (s_.substr(i_, 2) != "\\u") return Fail("unpaired high surrogate");
          i_ += 2;
          uint32_t lo = 0;
          if (!Hex4(&lo)) return false;
          if (lo < 0xDC00 || lo > 0xDFFF) return Fail("invalid low surrogate");
          cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        AppendUtf8(cp, out);
        return true;
      }
      default:
        --i_;
        return Fail("invalid escape");
    }
  }

  // RFC 3629 표 기준 UTF-8 한 글자 검증 후 그대로 복사.
  bool CopyUtf8(std::string* out) {
    unsigned char b0 = Peek();
    int len;
    unsigned char lo = 0x80, hi = 0xBF;  // 두 번째 바이트 범위
    if (b0 >= 0xC2 && b0 <= 0xDF) len = 2;
    else if (b0 == 0xE0) { len = 3; lo = 0xA0; }
    else if (b0 >= 0xE1 && b0 <= 0xEC) len = 3;
    else if (b0 == 0xED) { len = 3; hi = 0x9F; }
    else if (b0 >= 0xEE && b0 <= 0xEF) len = 3;
    else if (b0 == 0xF0) { len = 4; lo = 0x90; }
    else if (b0 >= 0xF1 && b0 <= 0xF3) len = 4;
    else if (b0 == 0xF4) { len = 4; hi = 0x8F; }
    else return Fail("invalid UTF-8");
    if (s_.size() - i_ < static_cast<size_t>(len)) return Fail("truncated UTF-8");
    for (int k = 1; k < len; ++k) {
      unsigned char b = static_cast<unsigned char>(s_[i_ + k]);
      unsigned char l = (k == 1) ? lo : 0x80, h = (k == 1) ? hi : 0xBF;
      if (b < l || b > h) return Fail("invalid UTF-8");
    }
    out->append(s_.substr(i_, len));
    i_ += len;
    return true;
  }

  bool ParseString(std::string* out) {
    out->clear();
    ++i_;  // '"'
    for (;;) {
      if (AtEnd()) return Fail("unterminated string");
      unsigned char c = Peek();
      if (c == '"') { ++i_; return true; }
      if (c == '\\') {
        if (!ParseEscape(out)) return false;
      } else if (c < 0x20) {
        return Fail("unescaped control character in string");
      } else if (c < 0x80) {
        out->push_back(static_cast<char>(c));
        ++i_;
      } else if (!CopyUtf8(out)) {
        return false;
      }
    }
  }

  std::string_view s_;
  size_t i_ = 0;
  std::string* err_;
};

void WriteString(std::string_view s, std::string* out) {
  static const char kHex[] = "0123456789abcdef";
  out->push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"': out->append("\\\""); break;
      case '\\': out->append("\\\\"); break;
      case '\b': out->append("\\b"); break;
      case '\f': out->append("\\f"); break;
      case '\n': out->append("\\n"); break;
      case '\r': out->append("\\r"); break;
      case '\t': out->append("\\t"); break;
      default:
        if (c < 0x20) {
          out->append("\\u00");
          out->push_back(kHex[c >> 4]);
          out->push_back(kHex[c & 0xF]);
        } else {
          out->push_back(static_cast<char>(c));
        }
    }
  }
  out->push_back('"');
}

void WriteCanonical(const Value& v, std::string* out) {
  switch (v.type) {
    case Value::Type::Object: {
      out->push_back('{');
      bool first = true;
      for (const auto& [k, child] : v.members) {
        if (!first) out->push_back(',');
        first = false;
        WriteString(k, out);
        out->push_back(':');
        WriteCanonical(child, out);
      }
      out->push_back('}');
      break;
    }
    case Value::Type::String: WriteString(v.str, out); break;
    case Value::Type::Int: out->append(std::to_string(v.num)); break;
    case Value::Type::Bool: out->append(v.boolean ? "true" : "false"); break;
  }
}

size_t Rank(const std::vector<std::string>& rank, const std::string& key) {
  auto it = std::find(rank.begin(), rank.end(), key);
  return static_cast<size_t>(it - rank.begin());
}

void WritePretty(const Value& v, const std::vector<std::string>& rank, int indent,
                 std::string* out) {
  if (v.type != Value::Type::Object) { WriteCanonical(v, out); return; }
  if (v.members.empty()) { out->append("{}"); return; }
  std::vector<const std::pair<std::string, Value>*> order;
  for (const auto& m : v.members) order.push_back(&m);
  // members는 이미 키 오름차순이므로 stable_sort로 순위 밖 키는 사전순이 유지된다.
  std::stable_sort(order.begin(), order.end(), [&](auto* a, auto* b) {
    return Rank(rank, a->first) < Rank(rank, b->first);
  });
  out->append("{\n");
  for (size_t k = 0; k < order.size(); ++k) {
    out->append(static_cast<size_t>(indent + 2), ' ');
    WriteString(order[k]->first, out);
    out->append(": ");
    WritePretty(order[k]->second, rank, indent + 2, out);
    if (k + 1 < order.size()) out->push_back(',');
    out->push_back('\n');
  }
  out->append(static_cast<size_t>(indent), ' ');
  out->push_back('}');
}

}  // namespace

const Value* Value::Find(std::string_view key) const {
  auto it = std::lower_bound(members.begin(), members.end(), key,
                             [](const auto& m, std::string_view k) { return m.first < k; });
  return (it != members.end() && it->first == key) ? &it->second : nullptr;
}

void Value::Erase(std::string_view key) {
  auto it = std::lower_bound(members.begin(), members.end(), key,
                             [](const auto& m, std::string_view k) { return m.first < k; });
  if (it != members.end() && it->first == key) members.erase(it);
}

void Value::Set(std::string key, Value v) {
  auto it = std::lower_bound(members.begin(), members.end(), key,
                             [](const auto& m, const std::string& k) { return m.first < k; });
  if (it != members.end() && it->first == key) {
    it->second = std::move(v);
  } else {
    members.emplace(it, std::move(key), std::move(v));
  }
}

Value MakeString(std::string s) {
  Value v;
  v.type = Value::Type::String;
  v.str = std::move(s);
  return v;
}

Value MakeInt(int64_t n) {
  Value v;
  v.type = Value::Type::Int;
  v.num = n;
  return v;
}

bool Parse(std::string_view in, Value* out, std::string* err) {
  Value tmp;
  if (!Parser(in, err).ParseDocument(&tmp)) return false;
  *out = std::move(tmp);
  return true;
}

std::string Canonicalize(const Value& v) {
  std::string out;
  WriteCanonical(v, &out);
  return out;
}

std::string Pretty(const Value& v, const std::vector<std::string>& key_rank) {
  std::string out;
  WritePretty(v, key_rank, 0, &out);
  return out;
}

}  // namespace stt::license::json

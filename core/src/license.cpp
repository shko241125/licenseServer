#include <chrono>
#include <cstdio>

#include "detail.h"
#include "json.h"

namespace stt::license {
namespace {

using json::Value;

// 사람이 읽는 license.lic 의 키 출력 순서 (원문 §3 순서 + 추가 필드). 서명과는 무관하다.
const std::vector<std::string>& KeyRank() {
  static const std::vector<std::string> rank = {
      "format_version", "license_id", "project_name", "license_type", "site_id",
      "allowed_channels", "online_stt", "offline_stt",
      "validity", "issued_at", "not_before", "not_after", "grace_period_days",
      "warning_notice", "signature"};
  return rank;
}

// Howard Hinnant, "chrono-Compatible Low-Level Date Algorithms" — days_from_civil / civil_from_days
int64_t DaysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void CivilFromDays(int64_t z, int64_t* y, unsigned* m, unsigned* d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  *d = doy - (153 * mp + 2) / 5 + 1;
  *m = mp < 10 ? mp + 3 : mp - 9;
  *y = static_cast<int64_t>(yoe) + era * 400 + (*m <= 2);
}

unsigned DaysInMonth(int64_t y, unsigned m) {
  static const unsigned kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return (m == 2 && leap) ? 29 : kDays[m - 1];
}

bool Digits(std::string_view s, size_t pos, size_t n, int* out) {
  int v = 0;
  for (size_t i = pos; i < pos + n; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    v = v * 10 + (s[i] - '0');
  }
  *out = v;
  return true;
}

// 필드 추출 도우미. 실패 시 detail에 필드 경로를 남긴다.
class Extractor {
 public:
  explicit Extractor(std::string* detail) : detail_(detail) {}

  const Value* Object(const Value& parent, const char* key) {
    const Value* v = parent.Find(key);
    if (!v || v->type != Value::Type::Object) return Fail(key, "must be an object"), nullptr;
    return v;
  }

  bool Text(const Value& parent, const char* key, size_t max_len, std::string* out) {
    const Value* v = parent.Find(key);
    if (!v || v->type != Value::Type::String) return Fail(key, "must be a string");
    if (v->str.empty() || v->str.size() > max_len) return Fail(key, "length out of range");
    for (unsigned char c : v->str) {
      if (c < 0x20 || c == 0x7F) return Fail(key, "must not contain control characters");
    }
    *out = v->str;
    return true;
  }

  bool Int(const Value& parent, const char* key, int64_t lo, int64_t hi, int* out) {
    const Value* v = parent.Find(key);
    if (!v || v->type != Value::Type::Int) return Fail(key, "must be an integer");
    if (v->num < lo || v->num > hi) return Fail(key, "out of range");
    *out = static_cast<int>(v->num);
    return true;
  }

  bool Time(const Value& parent, const char* key, int64_t* out) {
    const Value* v = parent.Find(key);
    if (!v || v->type != Value::Type::String || !ParseUtcTime(v->str, out)) {
      return Fail(key, "must be \"YYYY-MM-DDTHH:MM:SSZ\" (UTC)");
    }
    return true;
  }

  bool Fail(const char* key, const char* why) {
    if (detail_) *detail_ = std::string(key) + ": " + why;
    return false;
  }

 private:
  std::string* detail_;
};

Error ExtractFields(const Value& root, LicenseData* out, std::string* detail) {
  Extractor x(detail);
  LicenseData d;
  // format_version을 먼저 본다: 미래 형식이면 다른 필드 오류보다 "미지원 버전"이 정확한 사유다.
  const Value* fv = root.Find("format_version");
  if (!fv || fv->type != Value::Type::Int) {
    x.Fail("format_version", "must be an integer");
    return Error::InvalidField;
  }
  if (fv->num != kFormatVersion) {
    if (detail) *detail = "format_version " + std::to_string(fv->num) + " is not supported";
    return Error::UnsupportedVersion;
  }
  d.format_version = kFormatVersion;

  const Value* ch = nullptr;
  const Value* va = nullptr;
  bool ok = x.Text(root, "license_id", 128, &d.license_id) &&
            x.Text(root, "project_name", 256, &d.project_name) &&
            x.Text(root, "license_type", 64, &d.license_type) &&
            x.Text(root, "site_id", 256, &d.site_id) &&
            x.Text(root, "warning_notice", 4096, &d.warning_notice) &&
            (ch = x.Object(root, "allowed_channels")) != nullptr &&
            x.Int(*ch, "online_stt", 0, kMaxChannels, &d.online) &&
            x.Int(*ch, "offline_stt", 0, kMaxChannels, &d.offline) &&
            (va = x.Object(root, "validity")) != nullptr &&
            x.Time(*va, "issued_at", &d.issued_at) &&
            x.Time(*va, "not_before", &d.not_before) &&
            x.Time(*va, "not_after", &d.not_after) &&
            x.Int(*va, "grace_period_days", 0, kMaxGraceDays, &d.grace_days);
  if (!ok) return Error::InvalidField;
  if (d.online == 0 && d.offline == 0) {
    x.Fail("allowed_channels", "at least one channel type must be greater than 0");
    return Error::InvalidField;
  }
  if (d.not_before >= d.not_after) {
    x.Fail("validity", "not_before must be earlier than not_after");
    return Error::InvalidField;
  }
  *out = std::move(d);
  return Error::Ok;
}

}  // namespace

State LicenseData::StateAt(int64_t now) const {
  if (now < not_before) return State::NotYetValid;
  if (now <= not_after) return State::Valid;
  if (now <= GraceUntil()) return State::Grace;
  return State::Expired;
}

bool ParseUtcTime(std::string_view s, int64_t* out) {
  // 0123456789012345678 9
  // YYYY-MM-DDTHH:MM:SSZ
  if (s.size() != 20 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' ||
      s[16] != ':' || s[19] != 'Z') {
    return false;
  }
  int y, mo, d, h, mi, se;
  if (!Digits(s, 0, 4, &y) || !Digits(s, 5, 2, &mo) || !Digits(s, 8, 2, &d) ||
      !Digits(s, 11, 2, &h) || !Digits(s, 14, 2, &mi) || !Digits(s, 17, 2, &se)) {
    return false;
  }
  if (y < 1970 || mo < 1 || mo > 12 || d < 1 ||
      static_cast<unsigned>(d) > DaysInMonth(y, static_cast<unsigned>(mo)) || h > 23 ||
      mi > 59 || se > 59) {
    return false;
  }
  *out = DaysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 +
         h * 3600 + mi * 60 + se;
  return true;
}

std::string FormatUtcTime(int64_t t) {
  int64_t days = t >= 0 ? t / 86400 : (t - 86399) / 86400;
  int64_t secs = t - days * 86400;
  int64_t y;
  unsigned m, d;
  CivilFromDays(days, &y, &m, &d);
  char buf[64];  // int64 연도(최대 20자) + 나머지 16자를 모두 담는 크기
  std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02d:%02d:%02dZ", static_cast<long long>(y), m,
                d, static_cast<int>(secs / 3600), static_cast<int>(secs / 60 % 60),
                static_cast<int>(secs % 60));
  return buf;
}

int64_t SystemNow() {
  using namespace std::chrono;
  return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

Error VerifyTree(std::string_view bytes, const uint8_t pub[crypto::kPublicKeySize], Value* root_out,
                 std::string* canonical_out, std::string* detail) {
  if (bytes.size() > kMaxFileSize) return Error::FileTooLarge;
  if (bytes.substr(0, 3) == "\xEF\xBB\xBF") bytes.remove_prefix(3);  // 윈도우 메모장 BOM

  Value root;
  if (!json::Parse(bytes, &root, detail)) return Error::ParseError;
  if (root.type != Value::Type::Object) {
    if (detail) *detail = "top-level value must be an object";
    return Error::ParseError;
  }

  const Value* sig_v = root.Find("signature");
  std::vector<uint8_t> sig;
  if (!sig_v || sig_v->type != Value::Type::String || !crypto::Base64Decode(sig_v->str, &sig) ||
      sig.size() != crypto::kSignatureSize) {
    if (detail) *detail = "signature must be standard Base64 of 64 bytes";
    return Error::BadSignature;
  }
  root.Erase("signature");

  std::string canonical = json::Canonicalize(root);
  if (!crypto::Ed25519Verify(pub, canonical, sig.data())) {
    if (detail) *detail = "signature does not match license contents";
    return Error::BadSignature;
  }
  if (root_out) *root_out = std::move(root);
  if (canonical_out) *canonical_out = std::move(canonical);
  return Error::Ok;
}

Error ParseAndVerify(std::string_view bytes, const uint8_t pub[crypto::kPublicKeySize],
                     LicenseData* out, std::string* detail) {
  Value root;
  if (Error e = VerifyTree(bytes, pub, &root, nullptr, detail); e != Error::Ok) return e;
  // 이 시점 이후의 값은 모두 서명으로 보증된 트리에서만 꺼낸다.
  return ExtractFields(root, out, detail);
}

// ---- 서명 전용 검증 (공개 API) ----

Error VerifySignatureWith(const std::string& license_text, const uint8_t pub[crypto::kPublicKeySize],
                          VerifiedLicense* out, std::string* detail) {
  auto data = std::make_shared<detail::VerifiedData>();
  Error e = VerifyTree(license_text, pub, &data->root, &data->canonical, detail);
  if (e != Error::Ok) return e;
  detail::VerifiedAccess::Set(out, std::move(data));
  return Error::Ok;
}

VerifiedLicense::VerifiedLicense() = default;

namespace {

// "a.b.c" 경로를 객체 트리에서 찾는다. 중간이 객체가 아니거나 없으면 nullptr.
const Value* Lookup(const VerifiedLicense& v, const std::string& path) {
  const detail::VerifiedData* d = detail::VerifiedAccess::Get(v);
  if (!d || path.empty()) return nullptr;
  const Value* cur = &d->root;
  size_t pos = 0;
  while (true) {
    size_t dot = path.find('.', pos);
    std::string_view key(path.data() + pos, (dot == std::string::npos ? path.size() : dot) - pos);
    if (key.empty() || cur->type != Value::Type::Object) return nullptr;
    cur = cur->Find(key);
    if (!cur) return nullptr;
    if (dot == std::string::npos) return cur;
    pos = dot + 1;
  }
}

const std::string kEmpty;

}  // namespace

const std::string& VerifiedLicense::canonical_json() const {
  const detail::VerifiedData* d = detail::VerifiedAccess::Get(*this);
  return d ? d->canonical : kEmpty;
}

bool VerifiedLicense::Has(const std::string& path) const { return Lookup(*this, path) != nullptr; }

bool VerifiedLicense::GetString(const std::string& path, std::string* out) const {
  const Value* v = Lookup(*this, path);
  if (!v || v->type != Value::Type::String) return false;
  *out = v->str;
  return true;
}

bool VerifiedLicense::GetInt(const std::string& path, std::int64_t* out) const {
  const Value* v = Lookup(*this, path);
  if (!v || v->type != Value::Type::Int) return false;
  *out = v->num;
  return true;
}

bool VerifiedLicense::GetBool(const std::string& path, bool* out) const {
  const Value* v = Lookup(*this, path);
  if (!v || v->type != Value::Type::Bool) return false;
  *out = v->boolean;
  return true;
}

Error ParseStandardFields(const VerifiedLicense& license, LicenseFields* out, std::string* detail) {
  const detail::VerifiedData* d = detail::VerifiedAccess::Get(license);
  if (!d) {
    if (detail) *detail = "license is not verified";
    return Error::InvalidField;
  }
  LicenseData l;
  if (Error e = ExtractFields(d->root, &l, detail); e != Error::Ok) return e;
  LicenseFields f;
  f.format_version = l.format_version;
  f.license_id = l.license_id;
  f.project_name = l.project_name;
  f.license_type = l.license_type;
  f.site_id = l.site_id;
  f.warning_notice = l.warning_notice;
  f.online_stt = l.online;
  f.offline_stt = l.offline;
  f.issued_at = l.issued_at;
  f.not_before = l.not_before;
  f.not_after = l.not_after;
  f.grace_period_days = l.grace_days;
  *out = std::move(f);
  return Error::Ok;
}

State StateAt(const LicenseFields& f, std::int64_t now) {
  LicenseData l;
  l.not_before = f.not_before;
  l.not_after = f.not_after;
  l.grace_days = f.grace_period_days;
  return l.StateAt(now);
}

Error VerifyLicenseWith(const std::string& license_text, const uint8_t pub[crypto::kPublicKeySize], int64_t now,
                        VerifiedLicense* out, LicenseFields* fields, std::string* detail) {
  VerifiedLicense v;
  if (Error e = VerifySignatureWith(license_text, pub, &v, detail); e != Error::Ok) return e;
  LicenseFields f;
  if (Error e = ParseStandardFields(v, &f, detail); e != Error::Ok) return e;
  switch (StateAt(f, now)) {
    case State::NotYetValid:
      if (detail) *detail = "license is not valid before " + FormatUtcTime(f.not_before);
      return Error::NotYetValid;
    case State::Expired:
      if (detail) *detail = "license and grace period ended at " + FormatUtcTime(
          f.not_after + int64_t{f.grace_period_days} * 86400);
      return Error::Expired;
    case State::Valid:
    case State::Grace:
      break;
  }
  *out = std::move(v);  // 모든 검증을 통과했을 때만 결과를 채운다
  if (fields) *fields = std::move(f);
  return Error::Ok;
}

Error VerifyWith(const std::string& license_text, VerifyMode mode, const uint8_t pub[crypto::kPublicKeySize],
                 int64_t now, VerifiedLicense* out, LicenseFields* fields, std::string* detail) {
  switch (mode) {
    case VerifyMode::SignatureOnly: return VerifySignatureWith(license_text, pub, out, detail);
    case VerifyMode::Full: return VerifyLicenseWith(license_text, pub, now, out, fields, detail);
  }
  if (detail) *detail = "unknown verify mode";
  return Error::Internal;
}

const char* VerifyModeName(VerifyMode mode) {
  return mode == VerifyMode::Full ? "full" : "signature";
}

bool ParseVerifyMode(const std::string& name, VerifyMode* mode) {
  if (name == "signature") { *mode = VerifyMode::SignatureOnly; return true; }
  if (name == "full") { *mode = VerifyMode::Full; return true; }
  return false;
}

bool Utf16ToUtf8(const std::uint16_t* s, std::size_t n, std::string* out) {
  std::string r;
  r.reserve(n * 3);
  for (std::size_t i = 0; i < n; ++i) {
    uint32_t cp = s[i];
    if (cp >= 0xDC00 && cp <= 0xDFFF) return false;  // 짝 없는 low surrogate
    if (cp >= 0xD800 && cp <= 0xDBFF) {
      if (i + 1 >= n || s[i + 1] < 0xDC00 || s[i + 1] > 0xDFFF) return false;  // 짝 없는 high surrogate
      cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00u);
      ++i;
    }
    if (cp < 0x80) {
      r.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      r.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      r.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      r.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      r.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      r.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      r.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      r.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  *out = std::move(r);
  return true;
}

Error SignContract(std::string_view contract_json, const uint8_t seed[crypto::kSeedSize],
                   int64_t now, std::string* license_text, LicenseData* out,
                   std::string* detail) {
  Value root;
  if (!json::Parse(contract_json, &root, detail)) return Error::ParseError;
  if (root.type != Value::Type::Object) {
    if (detail) *detail = "top-level value must be an object";
    return Error::ParseError;
  }
  if (root.Find("signature")) {
    if (detail) *detail = "contract must not contain \"signature\"";
    return Error::InvalidField;
  }
  if (!root.Find("format_version")) root.Set("format_version", json::MakeInt(kFormatVersion));
  if (!root.Find("license_id")) {
    uint8_t id[16];
    if (!crypto::RandomBytes(id, sizeof id)) return Error::Internal;
    static const char kHex[] = "0123456789abcdef";
    std::string hex;
    for (uint8_t b : id) { hex.push_back(kHex[b >> 4]); hex.push_back(kHex[b & 0xF]); }
    root.Set("license_id", json::MakeString(hex));
  }
  if (Value* va = const_cast<Value*>(root.Find("validity"));
      va && va->type == Value::Type::Object && !va->Find("issued_at")) {
    va->Set("issued_at", json::MakeString(FormatUtcTime(now)));
  }

  LicenseData data;
  if (Error e = ExtractFields(root, &data, detail); e != Error::Ok) return e;
  if (data.license_type != "production" && data.license_type != "trial" &&
      data.license_type != "poc") {
    if (detail) *detail = "license_type: must be one of production, trial, poc";
    return Error::InvalidField;
  }

  uint8_t sig[crypto::kSignatureSize];
  uint8_t pub[crypto::kPublicKeySize];
  if (!crypto::Ed25519Sign(seed, json::Canonicalize(root), sig) ||
      !crypto::Ed25519PublicFromSeed(seed, pub)) {
    if (detail) *detail = "signing failed";
    return Error::Internal;
  }
  root.Set("signature", json::MakeString(crypto::Base64Encode(sig, sizeof sig)));
  std::string text = json::Pretty(root, KeyRank()) + "\n";

  // 자기검증: 배포될 텍스트 그대로를 검증기 경로로 통과시킨다.
  LicenseData check;
  if (Error e = ParseAndVerify(text, pub, &check, detail); e != Error::Ok) {
    if (detail) *detail = "self-verification failed: " + *detail;
    return Error::Internal;
  }
  *license_text = std::move(text);
  if (out) *out = std::move(check);
  return Error::Ok;
}

}  // namespace stt::license

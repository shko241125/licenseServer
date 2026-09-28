#include <fstream>
#include <mutex>

#include "detail.h"
#include "json.h"

namespace stt::license {

namespace detail {
struct Shared {
  std::mutex mu;
  Config cfg;
  std::string path;
  std::string loaded_raw;  // 현재 적용 중인 라이선스 파일 바이트
  LicenseData lic;
  int used[2] = {0, 0};
  int64_t last_check = 0;
  Error last_reload_error = Error::Ok;
};
}  // namespace detail

namespace {

using detail::Shared;

int Idx(Kind k) { return k == Kind::Online ? 0 : 1; }

Error ReadFile(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return Error::FileNotFound;
  std::string buf(kMaxFileSize + 1, '\0');
  f.read(&buf[0], static_cast<std::streamsize>(buf.size()));
  if (f.bad()) return Error::FileNotFound;
  buf.resize(static_cast<size_t>(f.gcount()));
  if (buf.size() > kMaxFileSize) return Error::FileTooLarge;
  *out = std::move(buf);
  return Error::Ok;
}

// 파일을 읽어 검증하고, 지금 시각에 사용할 수 있는 라이선스면 적용한다. s->mu 보유 상태에서 호출.
// 실패하면 기존 라이선스를 그대로 두고 오류를 기록한다(다음 점검 때 다시 시도).
// 만료·미개시 라이선스로의 교체는 거부한다: 사용 중인 유효 라이선스를 잘못된 파일이 덮지 않게 하기 위함.
// 미개시 갱신본을 미리 넣어 두면, not_before가 지난 뒤의 점검에서 자동으로 적용된다.
Error LoadLocked(Shared* s, int64_t now, bool force) {
  std::string raw;
  Error e = ReadFile(s->path, &raw);
  if (e == Error::Ok && !force && raw == s->loaded_raw) {
    s->last_reload_error = Error::Ok;
    return Error::Ok;
  }
  LicenseData next;
  if (e == Error::Ok) e = ParseAndVerify(raw, s->cfg.public_key.data(), &next, nullptr);
  if (e == Error::Ok) {
    State st = next.StateAt(now);
    if (st == State::NotYetValid) e = Error::NotYetValid;
    if (st == State::Expired) e = Error::Expired;
  }
  s->last_reload_error = e;
  if (e != Error::Ok) return e;
  s->lic = std::move(next);
  s->loaded_raw = std::move(raw);
  return Error::Ok;
}

void MaybeRecheckLocked(Shared* s, int64_t now) {
  // 시계가 뒤로 간 경우(now < last_check)에도 점검한다.
  if (now - s->last_check < s->cfg.recheck_seconds && now >= s->last_check) return;
  s->last_check = now;
  LoadLocked(s, now, false);
}

}  // namespace

// ---- Channel ----

Channel::Channel(Channel&& other) noexcept
    : shared_(std::move(other.shared_)), kind_(other.kind_) {}

Channel& Channel::operator=(Channel&& other) noexcept {
  if (this != &other) {
    Release();
    shared_ = std::move(other.shared_);
    kind_ = other.kind_;
  }
  return *this;
}

Channel::~Channel() { Release(); }

void Channel::Release() noexcept {
  if (!shared_) return;
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    int& used = shared_->used[Idx(kind_)];
    if (used > 0) --used;
  }
  shared_.reset();
}

// ---- Manager ----

Manager::Manager(std::shared_ptr<detail::Shared> shared) : shared_(std::move(shared)) {}
Manager::~Manager() = default;

Error Manager::OpenWith(const std::string& path, const detail::Config& config,
                        std::unique_ptr<Manager>* out) {
  auto s = std::make_shared<Shared>();
  s->cfg = config;
  s->path = path;
  const int64_t now = s->cfg.now();
  s->last_check = now;
  if (Error e = LoadLocked(s.get(), now, true); e != Error::Ok) return e;
  out->reset(new Manager(std::move(s)));
  return Error::Ok;
}

Error Manager::Acquire(Kind kind, Channel* out) {
  out->Release();  // 잠금 전에: 같은 Manager의 채널이면 mu를 다시 잡기 때문
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  const int64_t now = s->cfg.now();
  MaybeRecheckLocked(s, now);
  switch (s->lic.StateAt(now)) {
    case State::NotYetValid: return Error::NotYetValid;
    case State::Expired: return Error::Expired;
    case State::Valid:
    case State::Grace: break;
  }
  int& used = s->used[Idx(kind)];
  if (used >= s->lic.Max(kind)) return Error::ChannelLimit;
  ++used;
  out->shared_ = shared_;
  out->kind_ = kind;
  return Error::Ok;
}

Error Manager::Reload() {
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  const int64_t now = s->cfg.now();
  s->last_check = now;
  return LoadLocked(s, now, true);
}

int Manager::MaxChannels(Kind kind) {
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  MaybeRecheckLocked(s, s->cfg.now());
  return s->lic.Max(kind);
}

Info Manager::Snapshot() {
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  const int64_t now = s->cfg.now();
  MaybeRecheckLocked(s, now);
  const LicenseData& l = s->lic;
  Info i;
  i.license_id = l.license_id;
  i.project_name = l.project_name;
  i.license_type = l.license_type;
  i.site_id = l.site_id;
  i.online_max = l.online;
  i.offline_max = l.offline;
  i.online_used = s->used[0];
  i.offline_used = s->used[1];
  i.issued_at = l.issued_at;
  i.not_before = l.not_before;
  i.not_after = l.not_after;
  i.grace_until = l.GraceUntil();
  i.checked_at = now;
  i.state = l.StateAt(now);
  i.last_reload_error = s->last_reload_error;
  return i;
}

// ---- 문자열 ----

std::string InfoToJson(const Info& i) {
  using json::MakeInt;
  using json::MakeString;
  json::Value ch, root;
  ch.Set("online_max", MakeInt(i.online_max));
  ch.Set("online_used", MakeInt(i.online_used));
  ch.Set("offline_max", MakeInt(i.offline_max));
  ch.Set("offline_used", MakeInt(i.offline_used));
  root.Set("license_id", MakeString(i.license_id));
  root.Set("project_name", MakeString(i.project_name));
  root.Set("license_type", MakeString(i.license_type));
  root.Set("site_id", MakeString(i.site_id));
  root.Set("channels", std::move(ch));
  root.Set("issued_at", MakeString(FormatUtcTime(i.issued_at)));
  root.Set("not_before", MakeString(FormatUtcTime(i.not_before)));
  root.Set("not_after", MakeString(FormatUtcTime(i.not_after)));
  root.Set("grace_until", MakeString(FormatUtcTime(i.grace_until)));
  root.Set("checked_at", MakeString(FormatUtcTime(i.checked_at)));
  root.Set("state", MakeString(StateName(i.state)));
  root.Set("last_reload_error", MakeString(ErrorName(i.last_reload_error)));
  return json::Canonicalize(root);
}

const char* ErrorName(Error e) {
  switch (e) {
    case Error::Ok: return "OK";
    case Error::FileNotFound: return "LICENSE_FILE_NOT_FOUND";
    case Error::FileTooLarge: return "LICENSE_FILE_TOO_LARGE";
    case Error::ParseError: return "LICENSE_PARSE_ERROR";
    case Error::BadSignature: return "LICENSE_BAD_SIGNATURE";
    case Error::InvalidField: return "LICENSE_INVALID_FIELD";
    case Error::UnsupportedVersion: return "LICENSE_UNSUPPORTED_VERSION";
    case Error::NotYetValid: return "LICENSE_NOT_YET_VALID";
    case Error::Expired: return "LICENSE_EXPIRED";
    case Error::ChannelLimit: return "LICENSE_CHANNEL_LIMIT";
    case Error::Internal: return "LICENSE_INTERNAL_ERROR";
  }
  return "LICENSE_UNKNOWN_ERROR";
}

const char* ErrorMessage(Error e) {
  switch (e) {
    case Error::Ok: return "OK";
    case Error::FileNotFound:
      return "License file not found or unreadable. Place license.lic at LICENSE_FILE_PATH.";
    case Error::FileTooLarge:
      return "License file is larger than 64 KiB. Place the original license.lic.";
    case Error::ParseError:
      return "License file is not valid license JSON. Place the original license.lic.";
    case Error::BadSignature:
      return "License signature is invalid (file modified or not issued by vendor). "
             "Place the original license.lic.";
    case Error::InvalidField:
      return "License contents are invalid. Contact the vendor for a reissued license.";
    case Error::UnsupportedVersion:
      return "License format is newer than this engine supports. Upgrade the engine or "
             "request a compatible license.";
    case Error::NotYetValid:
      return "License is not valid yet (before not_before). Check the host clock.";
    case Error::Expired:
      return "License and grace period have expired. Install a renewed license.";
    case Error::ChannelLimit: return "All licensed channels are in use.";
    case Error::Internal: return "Internal license error (embedded key or crypto failure).";
  }
  return "Unknown license error.";
}

const char* StateName(State s) {
  switch (s) {
    case State::NotYetValid: return "NOT_YET_VALID";
    case State::Valid: return "VALID";
    case State::Grace: return "GRACE";
    case State::Expired: return "EXPIRED";
  }
  return "UNKNOWN";
}

}  // namespace stt::license

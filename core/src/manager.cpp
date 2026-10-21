#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <map>
#include <mutex>
#include <set>
#include <utility>

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
  std::map<std::pair<std::string, std::string>, int> used;  // (항목, 세부 항목) → 사용 중 채널 수
  int used_total = 0;                                       // 모든 세부 항목 합 (total 상한 대상)
  int64_t last_check = 0;
  Error last_reload_error = Error::Ok;
};
}  // namespace detail

namespace {

using detail::Shared;

// 일반 파일만 읽는다. FIFO·장치 파일이면 읽기가 무기한 멈출 수 있고(Acquire 가 뮤텍스를 쥔 채로),
// O_CLOEXEC 가 없으면 읽는 동안 fork 된 자식(JVM 의 ProcessBuilder 등)에 fd 가 샌다.
Error ReadFile(const std::string& path, std::string* out) {
  int fd;
  do {
    fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) return Error::FileNotFound;
  struct Closer { int fd; ~Closer() { ::close(fd); } } closer{fd};

  struct stat st;
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) return Error::FileNotFound;
  if (st.st_size > static_cast<off_t>(kMaxFileSize)) return Error::FileTooLarge;

  std::string buf(kMaxFileSize + 1, '\0');
  size_t len = 0;
  while (len < buf.size()) {  // 크기가 stat 이후 바뀌어도 상한을 지킨다
    ssize_t n = ::read(fd, &buf[len], buf.size() - len);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) return Error::FileNotFound;
    if (n == 0) break;
    len += static_cast<size_t>(n);
  }
  if (len > kMaxFileSize) return Error::FileTooLarge;
  buf.resize(len);
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
    : shared_(std::move(other.shared_)), group_(std::move(other.group_)), engine_(std::move(other.engine_)) {}

Channel& Channel::operator=(Channel&& other) noexcept {
  if (this != &other) {
    Release();
    shared_ = std::move(other.shared_);
    group_ = std::move(other.group_);
    engine_ = std::move(other.engine_);
  }
  return *this;
}

Channel::~Channel() { Release(); }

void Channel::Release() noexcept {
  if (!shared_) return;
  {
    std::lock_guard<std::mutex> lock(shared_->mu);
    auto it = shared_->used.find({group_, engine_});
    if (it != shared_->used.end() && it->second > 0) {
      if (--it->second == 0) shared_->used.erase(it);
      if (shared_->used_total > 0) --shared_->used_total;
    }
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

Error Manager::Acquire(const std::string& group, const std::string& engine, Channel* out) {
  out->Release();  // 잠금 전에: 같은 Manager의 채널이면 mu를 다시 잡기 때문
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  const int64_t now = s->cfg.now();
  MaybeRecheckLocked(s, now);
  switch (s->lic.StateAt(now)) {
    case State::NotYetValid: return Error::NotYetValid;
    case State::Expired: return Error::Expired;
    case State::Valid: break;
  }
  const int cap = s->lic.Max(group, engine);
  if (cap <= 0) return Error::EngineNotLicensed;
  const std::pair<std::string, std::string> key{group, engine};
  auto it = s->used.find(key);
  const int used = it == s->used.end() ? 0 : it->second;
  // 라이선스가 축소 교체되면 used 가 상한보다 클 수 있다: 사용 중인 채널은 유지하고 신규만 거부한다.
  if (used >= cap || s->used_total >= s->lic.total) return Error::ChannelLimit;
  ++s->used[key];
  ++s->used_total;
  out->shared_ = shared_;
  out->group_ = group;
  out->engine_ = engine;
  return Error::Ok;
}

Error Manager::Reload() {
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  const int64_t now = s->cfg.now();
  s->last_check = now;
  return LoadLocked(s, now, true);
}

int Manager::MaxChannels(const std::string& group, const std::string& engine) {
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  MaybeRecheckLocked(s, s->cfg.now());
  return s->lic.Max(group, engine);
}

int Manager::TotalChannels() {
  Shared* s = shared_.get();
  std::lock_guard<std::mutex> lock(s->mu);
  MaybeRecheckLocked(s, s->cfg.now());
  return s->lic.total;
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
  i.solution = l.solution;
  i.site_id = l.site_id;
  i.total_max = l.total;
  i.total_used = s->used_total;
  // 라이선스에 있는 세부 항목 + (교체로 빠졌지만) 아직 사용 중인 세부 항목. 항목·세부 항목 이름순.
  std::set<std::pair<std::string, std::string>> keys;
  for (const auto& [group, items] : l.groups) {
    for (const auto& [name, n] : items) keys.insert({group, name});
  }
  for (const auto& [key, n] : s->used) keys.insert(key);
  for (const auto& key : keys) {
    auto it = s->used.find(key);
    i.engines.push_back({key.first, key.second, l.Max(key.first, key.second), it == s->used.end() ? 0 : it->second});
  }
  i.issued_at = l.issued_at;
  i.not_before = l.not_before;
  i.not_after = l.not_after;
  i.checked_at = now;
  i.state = l.StateAt(now);
  i.check_mode = l.check_mode;
  i.last_reload_error = s->last_reload_error;
  return i;
}

// ---- 문자열 ----

std::string InfoToJson(const Info& i) {
  using json::MakeInt;
  using json::MakeString;
  auto usage = [](int max, int used) {
    json::Value u;
    u.Set("max", MakeInt(max));
    u.Set("used", MakeInt(used));
    return u;
  };
  json::Value ch, root;
  ch.Set("total", usage(i.total_max, i.total_used));
  std::map<std::string, json::Value> groups;
  for (const ChannelUsage& e : i.engines) groups[e.group].Set(e.engine, usage(e.max, e.used));
  for (auto& [group, items] : groups) ch.Set(group, std::move(items));
  root.Set("license_id", MakeString(i.license_id));
  root.Set("project_name", MakeString(i.project_name));
  root.Set("license_type", MakeString(i.license_type));
  root.Set("solution", MakeString(i.solution));
  root.Set("site_id", MakeString(i.site_id));
  root.Set("channels", std::move(ch));
  // minimal 판정에서 issued_at 을 읽지 못하면 0 이다. 1970-01-01 로 보이지 않게 빈 문자열로 둔다.
  const bool no_issued_at = i.check_mode == CheckMode::Minimal && i.issued_at == 0;
  root.Set("issued_at", MakeString(no_issued_at ? "" : FormatUtcTime(i.issued_at)));
  root.Set("not_before", MakeString(FormatUtcTime(i.not_before)));
  root.Set("not_after", MakeString(FormatUtcTime(i.not_after)));
  root.Set("checked_at", MakeString(FormatUtcTime(i.checked_at)));
  root.Set("state", MakeString(StateName(i.state)));
  root.Set("check_mode", MakeString(CheckModeName(i.check_mode)));
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
    case Error::EngineNotLicensed: return "LICENSE_ENGINE_NOT_LICENSED";
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
      return "License format (license_ver or check_mode) is not supported by this engine. "
             "Request a license in a format this engine supports.";
    case Error::NotYetValid:
      return "License is not valid yet (before not_before). Check the host clock.";
    case Error::Expired:
      return "License has expired (after not_after). Install a renewed license.";
    case Error::ChannelLimit: return "All licensed channels (engine or total) are in use.";
    case Error::Internal: return "Internal license error (embedded key or crypto failure).";
    case Error::EngineNotLicensed:
      return "This channel item is not licensed (not in allowed_channels or limit 0). Check the license.";
  }
  return "Unknown license error.";
}

const char* CheckModeName(CheckMode m) {
  return m == CheckMode::Minimal ? "minimal" : "full";
}

const char* StateName(State s) {
  switch (s) {
    case State::NotYetValid: return "NOT_YET_VALID";
    case State::Valid: return "VALID";
    case State::Expired: return "EXPIRED";
  }
  return "UNKNOWN";
}

}  // namespace stt::license

// licensectl — 사내 라이선스 발급기
//   licensectl keygen -out <dir>
//   licensectl issue  -key <private.key> -in <contract.json> -out <license.lic> [-ledger <dir>]
//   licensectl verify -pub <public.key> [-at YYYY-MM-DDTHH:MM:SSZ] <license.lic>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "crypto.h"
#include "detail.h"
#include "json.h"

namespace lic = stt::license;
namespace crypto = stt::license::crypto;

namespace {

int Usage() {
  std::fprintf(stderr,
               "usage:\n"
               "  licensectl keygen -out <dir>\n"
               "  licensectl issue  -key <private.key> -in <contract.json> -out <license.lic>"
               " [-ledger <dir>]\n"
               "  licensectl verify -pub <public.key> [-at YYYY-MM-DDTHH:MM:SSZ] <license.lic>\n");
  return 2;
}

int Fail(const std::string& msg) {
  std::fprintf(stderr, "error: %s\n", msg.c_str());
  return 1;
}

// "-name value" 쌍과 위치 인자를 분리한다.
bool ParseArgs(int argc, char** argv, std::map<std::string, std::string>* flags,
               std::vector<std::string>* positional) {
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a.size() > 1 && a[0] == '-') {
      if (i + 1 >= argc) return false;
      (*flags)[a.substr(1)] = argv[++i];
    } else {
      positional->push_back(a);
    }
  }
  return true;
}

bool ReadAll(const std::string& path, std::string* out) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  *out = ss.str();
  return !f.bad();
}

// 기존 파일을 덮어쓰지 않는다(O_EXCL).
bool WriteNew(const std::string& path, const std::string& data, mode_t mode, std::string* err) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, mode);
  if (fd < 0) {
    *err = path + ": " + std::strerror(errno);
    return false;
  }
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd, data.data() + off, data.size() - off);
    if (n < 0) {
      if (errno == EINTR) continue;
      *err = path + ": " + std::strerror(errno);
      ::close(fd);
      return false;
    }
    off += static_cast<size_t>(n);
  }
  if (::fsync(fd) != 0 || ::close(fd) != 0) {
    *err = path + ": " + std::strerror(errno);
    return false;
  }
  return true;
}

bool AppendLine(const std::string& path, const std::string& line, std::string* err) {
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
  if (fd < 0) {
    *err = path + ": " + std::strerror(errno);
    return false;
  }
  std::string data = line + "\n";
  bool ok = ::write(fd, data.data(), data.size()) == static_cast<ssize_t>(data.size()) &&
            ::fsync(fd) == 0;
  if (!ok) *err = path + ": " + std::strerror(errno);
  return ::close(fd) == 0 && ok;
}

bool Exists(const std::string& path) {
  struct stat st;
  return ::stat(path.c_str(), &st) == 0;
}

// 키 파일 형식
//   public.key : Base64(32바이트) 한 줄 (44자) — SDK 빌드(STT_LICENSE_PUBLIC_KEY_FILE)가 읽는 형식
//   private.key: "stt-license-ed25519-private-v1:" + Base64(32바이트 seed) 한 줄
// 비밀키에 접두어를 붙여, 공개키 자리(SDK 빌드·verify -pub)에 잘못 지정하면 형식 단계에서 거부되게 한다.
// (접두어가 없으면 둘 다 44자 Base64 라서 비밀키가 SDK 바이너리에 내장되어 배포될 수 있다)
constexpr char kPrivatePrefix[] = "stt-license-ed25519-private-v1:";

bool ReadKey(const std::string& path, bool is_private, std::vector<uint8_t>* out,
             std::string* err) {
  std::string text;
  if (!ReadAll(path, &text)) {
    *err = path + ": cannot read";
    return false;
  }
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
  const bool has_prefix = text.rfind(kPrivatePrefix, 0) == 0;
  if (is_private && !has_prefix) {
    *err = path + ": not a licensectl private key (missing \"" + kPrivatePrefix + "\" prefix)";
    return false;
  }
  if (!is_private && has_prefix) {
    *err = path + ": this is a PRIVATE key; pass public.key instead";
    return false;
  }
  if (is_private) text.erase(0, sizeof(kPrivatePrefix) - 1);
  const size_t size = is_private ? crypto::kSeedSize : crypto::kPublicKeySize;
  if (!crypto::Base64Decode(text, out) || out->size() != size) {
    *err = path + ": not a Base64 key of " + std::to_string(size) + " bytes";
    return false;
  }
  return true;
}

int Keygen(const std::map<std::string, std::string>& flags) {
  auto it = flags.find("out");
  if (it == flags.end()) return Usage();
  const std::string dir = it->second;
  if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
    return Fail(dir + ": " + std::strerror(errno));
  }
  const std::string priv_path = dir + "/private.key", pub_path = dir + "/public.key";
  if (Exists(priv_path) || Exists(pub_path)) {
    return Fail("key files already exist in " + dir + " (refusing to overwrite)");
  }
  uint8_t seed[crypto::kSeedSize], pub[crypto::kPublicKeySize];
  if (!crypto::RandomBytes(seed, sizeof seed) || !crypto::Ed25519PublicFromSeed(seed, pub)) {
    return Fail("key generation failed");
  }
  std::string err;
  if (!WriteNew(priv_path, kPrivatePrefix + crypto::Base64Encode(seed, sizeof seed) + "\n", 0600, &err) ||
      !WriteNew(pub_path, crypto::Base64Encode(pub, sizeof pub) + "\n", 0644, &err)) {
    return Fail(err);
  }
  // 권한을 지원하지 않는 파일시스템(WSL /mnt/c, FAT, 일부 네트워크 드라이브)에서는 0600 이 적용되지 않는다.
  struct stat st;
  if (::stat(priv_path.c_str(), &st) != 0 || (st.st_mode & 077) != 0) {
    std::fprintf(stderr,
                 "WARNING: %s is readable by other users (mode %03o). This filesystem does not "
                 "enforce permissions; move the key to a Linux filesystem with mode 0600.\n",
                 priv_path.c_str(), static_cast<unsigned>(st.st_mode & 0777));
  }
  std::printf("private key: %s (mode 0600, never leave the issuing host)\n", priv_path.c_str());
  std::printf("public key : %s (build SDK with -DSTT_LICENSE_PUBLIC_KEY_FILE=%s)\n",
              pub_path.c_str(), pub_path.c_str());
  return 0;
}

std::string SafeName(const std::string& s) {
  std::string r;
  for (char c : s) {
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_';
    r.push_back(ok ? c : '_');
  }
  return r;
}

void PrintSummary(const lic::LicenseData& d, int64_t at) {
  std::printf("license_id     : %s\n", d.license_id.c_str());
  std::printf("project_name   : %s\n", d.project_name.c_str());
  std::printf("license_type   : %s\n", d.license_type.c_str());
  std::printf("site_id        : %s\n", d.site_id.c_str());
  std::printf("channels       : online_stt=%d offline_stt=%d\n", d.online, d.offline);
  std::printf("valid          : %s ~ %s (grace until %s)\n", lic::FormatUtcTime(d.not_before).c_str(),
              lic::FormatUtcTime(d.not_after).c_str(), lic::FormatUtcTime(d.GraceUntil()).c_str());
  std::printf("state at %s: %s\n", lic::FormatUtcTime(at).c_str(), lic::StateName(d.StateAt(at)));
}

int Issue(const std::map<std::string, std::string>& flags) {
  for (const char* k : {"key", "in", "out"}) {
    if (!flags.count(k)) return Usage();
  }
  std::string err, contract;
  std::vector<uint8_t> seed;
  if (!ReadKey(flags.at("key"), true, &seed, &err)) return Fail(err);
  if (!ReadAll(flags.at("in"), &contract)) return Fail(flags.at("in") + ": cannot read");
  if (Exists(flags.at("out"))) return Fail(flags.at("out") + ": already exists");

  const int64_t now = lic::SystemNow();
  std::string text, detail;
  lic::LicenseData d;
  lic::Error e = lic::SignContract(contract, seed.data(), now, &text, &d, &detail);
  if (e != lic::Error::Ok) return Fail(std::string(lic::ErrorName(e)) + ": " + detail);
  if (!WriteNew(flags.at("out"), text, 0644, &err)) return Fail(err);

  auto ledger = flags.find("ledger");
  if (ledger != flags.end()) {
    const std::string& dir = ledger->second;
    if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
      return Fail(dir + ": " + std::strerror(errno));
    }
    std::string date = lic::FormatUtcTime(now).substr(0, 10);
    date.erase(std::remove(date.begin(), date.end(), '-'), date.end());
    const std::string copy = dir + "/" + date + "_" + SafeName(d.site_id) + "_" +
                             SafeName(d.license_id) + ".lic";
    namespace json = stt::license::json;
    json::Value row;
    const char* user = std::getenv("USER");
    row.Set("issued_by", json::MakeString(user && *user ? user : "unknown"));
    row.Set("recorded_at", json::MakeString(lic::FormatUtcTime(now)));
    row.Set("license_id", json::MakeString(d.license_id));
    row.Set("project_name", json::MakeString(d.project_name));
    row.Set("site_id", json::MakeString(d.site_id));
    row.Set("license_type", json::MakeString(d.license_type));
    row.Set("online_stt", json::MakeInt(d.online));
    row.Set("offline_stt", json::MakeInt(d.offline));
    row.Set("not_after", json::MakeString(lic::FormatUtcTime(d.not_after)));
    row.Set("file", json::MakeString(copy));
    if (!WriteNew(copy, text, 0600, &err) ||
        !AppendLine(dir + "/ledger.jsonl", json::Canonicalize(row), &err)) {
      return Fail("license written to " + flags.at("out") + " but ledger failed: " + err);
    }
  }
  std::printf("issued: %s\n", flags.at("out").c_str());
  PrintSummary(d, now);
  return 0;
}

int Verify(const std::map<std::string, std::string>& flags, const std::vector<std::string>& pos) {
  if (!flags.count("pub") || pos.size() != 1) return Usage();
  std::string err, bytes, detail;
  std::vector<uint8_t> pub;
  if (!ReadKey(flags.at("pub"), false, &pub, &err)) return Fail(err);
  if (!ReadAll(pos[0], &bytes)) return Fail(pos[0] + ": cannot read");
  int64_t at = lic::SystemNow();
  auto it = flags.find("at");
  if (it != flags.end() && !lic::ParseUtcTime(it->second, &at)) return Fail("-at: invalid time");

  lic::LicenseData d;
  lic::Error e = lic::ParseAndVerify(bytes, pub.data(), &d, &detail);
  if (e != lic::Error::Ok) {
    std::printf("INVALID %s: %s\n", lic::ErrorName(e), detail.c_str());
    return 1;
  }
  std::printf("SIGNATURE OK\n");
  PrintSummary(d, at);
  lic::State st = d.StateAt(at);
  return (st == lic::State::Valid || st == lic::State::Grace) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return Usage();
  std::map<std::string, std::string> flags;
  std::vector<std::string> positional;
  if (!ParseArgs(argc, argv, &flags, &positional)) return Usage();
  const std::string cmd = argv[1];
  if (cmd == "keygen") return Keygen(flags);
  if (cmd == "issue") return Issue(flags);
  if (cmd == "verify") return Verify(flags, positional);
  return Usage();
}

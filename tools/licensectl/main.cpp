// licensectl — 사내 라이선스 발급기
//   licensectl keygen -out <dir>
//   licensectl issue  -key <private.key> -in <contract.json> -out <license.lic> [-ledger <dir>]
//   licensectl verify (-pub <public.key> | -pubkey <base64>) [-at <UTC>] [-json] <license.lic | ->
//   licensectl sign     stdin: 1행 비밀키 문자열, 2행~EOF 계약 JSON → stdout: license.lic   (파일을 쓰지 않음)
//   licensectl pubkey   stdin: 비밀키 문자열 → stdout: 공개키 Base64
#include <openssl/crypto.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
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
               "  licensectl verify (-pub <public.key> | -pubkey <base64>) [-at YYYY-MM-DDTHH:MM:SSZ]"
               " [-json] <license.lic | ->\n"
               "  licensectl sign    < (private key line + contract JSON)   > license.lic\n"
               "  licensectl pubkey  < private key line                     > public key\n");
  return 2;
}

int Fail(const std::string& msg) {
  std::fprintf(stderr, "error: %s\n", msg.c_str());
  return 1;
}

// "-name value" 쌍, 값 없는 플래그(-json), 위치 인자("-" = stdin 포함)를 분리한다.
bool ParseArgs(int argc, char** argv, std::map<std::string, std::string>* flags,
               std::vector<std::string>* positional) {
  static const std::set<std::string> kBoolFlags = {"json"};
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    if (a.size() > 1 && a[0] == '-') {
      if (kBoolFlags.count(a.substr(1))) {
        (*flags)[a.substr(1)] = "1";
        continue;
      }
      if (i + 1 >= argc) return false;
      (*flags)[a.substr(1)] = argv[++i];
    } else {
      positional->push_back(a);
    }
  }
  return true;
}

// stdin 을 max 바이트까지 읽는다. 초과하면 false.
bool ReadStdin(size_t max, std::string* out) {
  std::string buf;
  char chunk[4096];
  size_t n;
  while ((n = std::fread(chunk, 1, sizeof chunk, stdin)) > 0) {
    buf.append(chunk, n);
    if (buf.size() > max) return false;
  }
  if (std::ferror(stdin)) return false;
  *out = std::move(buf);
  return true;
}

bool ReadAll(const std::string& path, std::string* out) {
  if (path == "-") return ReadStdin(lic::kMaxFileSize, out);
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

// 키 문자열(파일 내용 또는 stdin 한 줄)을 해석한다. label 은 오류 메시지용.
bool ParseKeyText(std::string text, const std::string& path, bool is_private,
                  std::vector<uint8_t>* out, std::string* err) {
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
    OPENSSL_cleanse(&text[0], text.size());
    return false;
  }
  OPENSSL_cleanse(&text[0], text.size());
  return true;
}

bool ReadKey(const std::string& path, bool is_private, std::vector<uint8_t>* out,
             std::string* err) {
  std::string text;
  if (!ReadAll(path, &text)) {
    *err = path + ": cannot read";
    return false;
  }
  return ParseKeyText(std::move(text), path, is_private, out, err);
}

bool WriteStdout(const std::string& data) {
  return std::fwrite(data.data(), 1, data.size(), stdout) == data.size() && std::fflush(stdout) == 0;
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
  OPENSSL_cleanse(seed.data(), seed.size());
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
  if (flags.count("pub") == flags.count("pubkey") || pos.size() != 1) return Usage();
  std::string err, bytes, detail;
  std::vector<uint8_t> pub;
  if (flags.count("pub")) {
    if (!ReadKey(flags.at("pub"), false, &pub, &err)) return Fail(err);
  } else if (!ParseKeyText(flags.at("pubkey"), "-pubkey", false, &pub, &err)) {
    return Fail(err);
  }
  if (!ReadAll(pos[0], &bytes)) return Fail(pos[0] + ": cannot read (or larger than 64 KiB)");
  int64_t at = lic::SystemNow();
  auto it = flags.find("at");
  if (it != flags.end() && !lic::ParseUtcTime(it->second, &at)) return Fail("-at: invalid time");

  lic::LicenseData d;
  lic::Error e = lic::ParseAndVerify(bytes, pub.data(), &d, &detail);
  const bool as_json = flags.count("json") > 0;
  const lic::State st = e == lic::Error::Ok ? d.StateAt(at) : lic::State::Expired;
  const int code = (e == lic::Error::Ok && (st == lic::State::Valid || st == lic::State::Grace)) ? 0 : 1;

  if (as_json) {  // 한 줄 JSON (서버 연동용). 값은 모두 서명 검증을 통과한 뒤에만 채운다.
    namespace json = stt::license::json;
    json::Value o;
    o.Set("result", json::MakeString(lic::ErrorName(e)));
    o.Set("checked_at", json::MakeString(lic::FormatUtcTime(at)));
    if (e != lic::Error::Ok) {
      o.Set("detail", json::MakeString(detail));
    } else {
      o.Set("state", json::MakeString(lic::StateName(st)));
      o.Set("license_id", json::MakeString(d.license_id));
      o.Set("project_name", json::MakeString(d.project_name));
      o.Set("license_type", json::MakeString(d.license_type));
      o.Set("site_id", json::MakeString(d.site_id));
      o.Set("online_stt", json::MakeInt(d.online));
      o.Set("offline_stt", json::MakeInt(d.offline));
      o.Set("issued_at", json::MakeString(lic::FormatUtcTime(d.issued_at)));
      o.Set("not_before", json::MakeString(lic::FormatUtcTime(d.not_before)));
      o.Set("not_after", json::MakeString(lic::FormatUtcTime(d.not_after)));
      o.Set("grace_period_days", json::MakeInt(d.grace_days));
      o.Set("grace_until", json::MakeString(lic::FormatUtcTime(d.GraceUntil())));
    }
    if (!WriteStdout(json::Canonicalize(o) + "\n")) return Fail("cannot write stdout");
    return code;
  }
  if (e != lic::Error::Ok) {
    std::printf("INVALID %s: %s\n", lic::ErrorName(e), detail.c_str());
    return 1;
  }
  std::printf("SIGNATURE OK\n");
  PrintSummary(d, at);
  return code;
}

// stdin 1행을 비밀키로 해석하고 나머지를 돌려준다. 호출자가 seed 를 지운다.
bool ReadPrivateKeyFromStdin(std::vector<uint8_t>* seed, std::string* rest, std::string* err) {
  std::string in;
  if (!ReadStdin(lic::kMaxFileSize + 256, &in)) {
    *err = "stdin: cannot read (or too large)";
    return false;
  }
  const size_t nl = in.find('\n');
  std::string key_line = in.substr(0, nl);
  if (rest) *rest = nl == std::string::npos ? std::string() : in.substr(nl + 1);
  OPENSSL_cleanse(&in[0], in.size());
  return ParseKeyText(std::move(key_line), "stdin", true, seed, err);
}

// 서버 연동용: 비밀키와 계약을 stdin 으로 받아 파일을 전혀 쓰지 않고 stdout 으로 라이선스를 낸다.
int Sign() {
  std::vector<uint8_t> seed;
  std::string contract, err;
  if (!ReadPrivateKeyFromStdin(&seed, &contract, &err)) return Fail(err);
  std::string text, detail;
  lic::Error e = lic::SignContract(contract, seed.data(), lic::SystemNow(), &text, nullptr, &detail);
  OPENSSL_cleanse(seed.data(), seed.size());
  if (e != lic::Error::Ok) return Fail(std::string(lic::ErrorName(e)) + ": " + detail);
  if (!WriteStdout(text)) return Fail("cannot write stdout");
  return 0;
}

int PubKey() {
  std::vector<uint8_t> seed;
  std::string rest, err;
  if (!ReadPrivateKeyFromStdin(&seed, &rest, &err)) return Fail(err);
  uint8_t pub[crypto::kPublicKeySize];
  const bool ok = crypto::Ed25519PublicFromSeed(seed.data(), pub);
  OPENSSL_cleanse(seed.data(), seed.size());
  if (!ok) return Fail("public key derivation failed");
  if (!WriteStdout(crypto::Base64Encode(pub, sizeof pub) + "\n")) return Fail("cannot write stdout");
  return 0;
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
  if (cmd == "sign" && flags.empty() && positional.empty()) return Sign();
  if (cmd == "pubkey" && flags.empty() && positional.empty()) return PubKey();
  return Usage();
}

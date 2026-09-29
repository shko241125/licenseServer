#include "crypto.h"

#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/rand.h>

// EVP_PKEY_new_raw_public_key / EVP_DigestVerify(one-shot) 는 1.1.1 에서 추가됐다.
// SDK 가 자체 OpenSSL 타깃(STT_LICENSE_CRYPTO_TARGET)을 넘기면 CMake 가 버전을 확인할 수 없으므로 여기서 막는다.
#if OPENSSL_VERSION_NUMBER < 0x10101000L
#error "stt_license requires OpenSSL 1.1.1 or later (libcrypto)"
#endif

#include <memory>

namespace stt::license::crypto {
namespace {

struct PkeyFree { void operator()(EVP_PKEY* p) const { EVP_PKEY_free(p); } };
struct MdCtxFree { void operator()(EVP_MD_CTX* p) const { EVP_MD_CTX_free(p); } };
using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyFree>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, MdCtxFree>;

const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int B64Index(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

}  // namespace

bool Ed25519Verify(const uint8_t pub[kPublicKeySize], std::string_view msg,
                   const uint8_t sig[kSignatureSize]) {
  PkeyPtr key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, pub, kPublicKeySize));
  MdCtxPtr ctx(EVP_MD_CTX_new());
  if (!key || !ctx) return false;
  if (EVP_DigestVerifyInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) return false;
  // EVP_DigestVerify: 1 = 일치, 0 = 불일치, 음수 = 오류. 1만 성공으로 본다.
  return EVP_DigestVerify(ctx.get(), sig, kSignatureSize,
                          reinterpret_cast<const unsigned char*>(msg.data()), msg.size()) == 1;
}

bool Ed25519Sign(const uint8_t seed[kSeedSize], std::string_view msg,
                 uint8_t sig_out[kSignatureSize]) {
  PkeyPtr key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed, kSeedSize));
  MdCtxPtr ctx(EVP_MD_CTX_new());
  if (!key || !ctx) return false;
  if (EVP_DigestSignInit(ctx.get(), nullptr, nullptr, nullptr, key.get()) != 1) return false;
  size_t len = kSignatureSize;
  if (EVP_DigestSign(ctx.get(), sig_out, &len, reinterpret_cast<const unsigned char*>(msg.data()),
                     msg.size()) != 1) {
    return false;
  }
  return len == kSignatureSize;
}

bool Ed25519PublicFromSeed(const uint8_t seed[kSeedSize], uint8_t pub_out[kPublicKeySize]) {
  PkeyPtr key(EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed, kSeedSize));
  if (!key) return false;
  size_t len = kPublicKeySize;
  return EVP_PKEY_get_raw_public_key(key.get(), pub_out, &len) == 1 && len == kPublicKeySize;
}

bool RandomBytes(uint8_t* buf, size_t n) { return RAND_bytes(buf, static_cast<int>(n)) == 1; }

std::string Base64Encode(const uint8_t* data, size_t n) {
  std::string out;
  out.reserve((n + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 3 <= n; i += 3) {
    uint32_t v = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(kB64[(v >> 6) & 63]);
    out.push_back(kB64[v & 63]);
  }
  if (n - i == 1) {
    uint32_t v = data[i] << 16;
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.append("==");
  } else if (n - i == 2) {
    uint32_t v = (data[i] << 16) | (data[i + 1] << 8);
    out.push_back(kB64[(v >> 18) & 63]);
    out.push_back(kB64[(v >> 12) & 63]);
    out.push_back(kB64[(v >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

bool Base64Decode(std::string_view in, std::vector<uint8_t>* out) {
  if (in.size() % 4 != 0) return false;
  size_t pad = 0;
  if (!in.empty() && in.back() == '=') pad = (in.size() >= 2 && in[in.size() - 2] == '=') ? 2 : 1;
  std::vector<uint8_t> res;
  res.reserve(in.size() / 4 * 3);
  for (size_t i = 0; i < in.size(); i += 4) {
    bool last = (i + 4 == in.size());
    int q[4];
    for (int k = 0; k < 4; ++k) {
      char c = in[i + k];
      if (last && c == '=' && k >= 4 - static_cast<int>(pad)) { q[k] = 0; continue; }
      q[k] = B64Index(c);
      if (q[k] < 0) return false;  // 공백, 중간 '=', 기타 문자
    }
    uint32_t v = (q[0] << 18) | (q[1] << 12) | (q[2] << 6) | q[3];
    res.push_back(static_cast<uint8_t>(v >> 16));
    if (!(last && pad == 2)) res.push_back(static_cast<uint8_t>(v >> 8));
    if (!(last && pad >= 1)) res.push_back(static_cast<uint8_t>(v));
    // 비정규 표기 거부: 패딩 직전 문자의 버려지는 비트가 0이어야 한다.
    if (last && pad == 2 && (q[1] & 0x0F) != 0) return false;
    if (last && pad == 1 && (q[2] & 0x03) != 0) return false;
  }
  *out = std::move(res);
  return true;
}

}  // namespace stt::license::crypto

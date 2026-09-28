// Ed25519·Base64·난수. OpenSSL 1.1.1 과 3.x 양쪽에 존재하는 EVP API만 사용한다.
// (STT SDK가 OpenSSL 1.1.1을 정적 링크하고 있어 같은 라이브러리를 재사용한다.)
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace stt::license::crypto {

constexpr size_t kPublicKeySize = 32;
constexpr size_t kSeedSize = 32;       // 비밀키 = RFC 8032의 32바이트 seed
constexpr size_t kSignatureSize = 64;

bool Ed25519Verify(const uint8_t pub[kPublicKeySize], std::string_view msg,
                   const uint8_t sig[kSignatureSize]);
bool Ed25519Sign(const uint8_t seed[kSeedSize], std::string_view msg,
                 uint8_t sig_out[kSignatureSize]);
bool Ed25519PublicFromSeed(const uint8_t seed[kSeedSize], uint8_t pub_out[kPublicKeySize]);
bool RandomBytes(uint8_t* buf, size_t n);

// 표준 Base64(RFC 4648 §4, 패딩 포함).
std::string Base64Encode(const uint8_t* data, size_t n);
// 엄격 디코드: 길이 4의 배수, 공백 불허, 패딩은 끝에만, 남는 비트는 0이어야 함.
bool Base64Decode(std::string_view in, std::vector<uint8_t>* out);

}  // namespace stt::license::crypto

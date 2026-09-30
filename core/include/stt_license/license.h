// stt_license — 폐쇄망 오프라인 라이선스 검증·집행 코어 (SDK 공개 헤더)
//
// 두 가지 사용 방식:
//  (A) 서명만 공통 검증하고 값 정책은 SDK 가 결정 — VerifySignature (JNI connect 의 String hostLicense 등)
//  (B) 파일 경로 + 값 정책(기간·채널 한도·재적재) 내장 — Manager
//
// (B) 사용 흐름 (SDK):
//   connectSonaSTT  → Manager::Open(path, &mgr)       실패 시 SDK 초기화 실패
//   getIdleSession  → mgr->Acquire(Kind::Offline, &ch) 실패 시 세션 예약 거부
//   작업 종료/중단   → ch.Release() 또는 Channel 소멸
//   getSTTSessionInfo → InfoToJson(mgr->Snapshot())
//
// 이 헤더의 어떤 함수도 예외를 던지지 않는다(std::bad_alloc 제외). 모든 실패는 Error 값이다.
// 스레드 안전: Manager 의 모든 멤버 함수와 Channel::Release 는 여러 스레드에서 동시에 호출해도 된다.
//              단, Manager 소멸은 다른 스레드의 Manager 호출이 모두 끝난 뒤에 해야 한다(Channel 은 예외).
// 언어 수준: 이 헤더는 C++11 이상에서 포함할 수 있다(라이브러리 구현은 C++17 로 빌드된다).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace stt {
namespace license {

enum class Kind { Online = 0, Offline = 1 };  // allowed_channels.online_stt / offline_stt

enum class State { NotYetValid, Valid, Grace, Expired };

// 순번은 JNI/API 오류 코드(1000 + 순번)로 쓰인다. 기존 값의 순서를 바꾸지 말고 새 값은 끝에 추가할 것.
enum class Error {
  Ok = 0,
  FileNotFound,        // 파일 없음/읽기 실패
  FileTooLarge,        // 64 KiB 초과
  ParseError,          // JSON 형식 위반(중복 키, 실수, null, 배열 등 포함)
  BadSignature,        // 서명 누락·형식 오류·불일치
  InvalidField,        // 필수 필드 누락, 값 범위·형식 위반
  UnsupportedVersion,  // format_version 미지원
  NotYetValid,         // not_before 이전
  Expired,             // 유예 기간까지 종료
  ChannelLimit,        // 허용 채널 모두 사용 중
  Internal,            // 내장 공개키 손상, 암호 라이브러리 실패 등
};
static_assert(static_cast<int>(Error::Internal) == 10, "Error codes are part of the API");

const char* ErrorName(Error e);     // 예: "LICENSE_BAD_SIGNATURE" (API 오류 코드로 사용)
const char* ErrorMessage(Error e);  // 현장 조치 문구 (로그용)
const char* StateName(State s);     // "VALID", "GRACE", ...

// 조회용 스냅샷. 서명값은 포함하지 않는다. 시각은 모두 UTC epoch 초.
struct Info {
  std::string license_id, project_name, license_type, site_id;
  int online_max = 0, offline_max = 0;
  int online_used = 0, offline_used = 0;
  std::int64_t issued_at = 0, not_before = 0, not_after = 0, grace_until = 0;
  std::int64_t checked_at = 0;                // 이 스냅샷을 만든 시각
  State state = State::Expired;
  Error last_reload_error = Error::Ok;    // 마지막 파일 재적재 실패 사유 (성공/미변경이면 Ok)
};

std::string InfoToJson(const Info& info);  // 한 줄 JSON

class Manager;
namespace detail { struct Config; struct Shared; struct VerifiedData; struct VerifiedAccess; }

// ---------------------------------------------------------------------------------------------
// (A) 서명 전용 검증
// ---------------------------------------------------------------------------------------------

// 서명 검증을 통과한 라이선스. 값은 서명된 내용에서만 꺼낼 수 있다(서명 범위 밖의 값은 존재하지 않음).
// 필수 필드·범위·기간은 검증하지 않았다 — 무엇을 어떻게 쓸지는 호출자(SDK)가 결정한다.
// 경로는 점으로 구분한다: "site_id", "allowed_channels.offline_stt", "validity.not_after".
class VerifiedLicense {
 public:
  VerifiedLicense();
  bool valid() const noexcept { return data_ != nullptr; }

  // 서명 대상 바이트(signature 를 뺀 RFC 8785 정규 JSON). SDK 의 JSON 라이브러리로 직접 해석해도 된다.
  const std::string& canonical_json() const;

  bool Has(const std::string& path) const;
  bool GetString(const std::string& path, std::string* out) const;   // 문자열이 아니거나 없으면 false
  bool GetInt(const std::string& path, std::int64_t* out) const;     // 정수가 아니거나 없으면 false
  bool GetBool(const std::string& path, bool* out) const;

 private:
  friend struct detail::VerifiedAccess;
  std::shared_ptr<const detail::VerifiedData> data_;
};

// 내장 공개키로 서명만 검증한다. 검사 항목: 크기(64 KiB), BOM 제거, 엄격 JSON 파싱(정규화에 필요),
// signature 형식(표준 Base64, 64바이트), Ed25519 서명. 실패 시 FileTooLarge / ParseError / BadSignature /
// Internal(내장 공개키 손상). license_text 는 표준 UTF-8 이어야 한다(JNI 에서는 Utf16ToUtf8 사용).
Error VerifySignature(const std::string& license_text, VerifiedLicense* out, std::string* detail = nullptr);

// 선택 도우미: 표준 스키마(v1) 규칙으로 필드를 해석·검증한다(필수 필드, 범위, format_version, 시각 형식).
// 쓸지 말지는 SDK 가 결정한다. 실패 시 InvalidField / UnsupportedVersion.
struct LicenseFields {
  int format_version = 0;
  std::string license_id, project_name, license_type, site_id, warning_notice;
  int online_stt = 0, offline_stt = 0;
  std::int64_t issued_at = 0, not_before = 0, not_after = 0;  // UTC epoch 초
  int grace_period_days = 0;
};
Error ParseStandardFields(const VerifiedLicense& license, LicenseFields* out, std::string* detail = nullptr);
State StateAt(const LicenseFields& fields, std::int64_t now_epoch_seconds);

// 서명 + 값 검증: VerifySignature → ParseStandardFields → 기간 판정(시스템 시각).
// NotYetValid·Expired 면 실패, Grace 는 성공(경고는 StateAt(*fields, now) 로 판단). fields 는 nullptr 가능.
Error VerifyLicense(const std::string& license_text, VerifiedLicense* out, LicenseFields* fields,
                    std::string* detail = nullptr);

// 하나의 진입점에서 검증 수준을 고른다(SDK 설정값에 따라 전환할 때). bool 대신 열거형을 받아 호출부가 읽힌다.
//   SignatureOnly: VerifySignature 와 같음 (fields 는 건드리지 않음) — 기존 SDK 정책을 유지할 때
//   Full         : VerifyLicense 와 같음 — 표준 필드·기간까지 코어 규칙으로 검증할 때
enum class VerifyMode { SignatureOnly = 0, Full = 1 };
Error Verify(const std::string& license_text, VerifyMode mode, VerifiedLicense* out,
             LicenseFields* fields = nullptr, std::string* detail = nullptr);
const char* VerifyModeName(VerifyMode mode);                       // "signature" / "full"
bool ParseVerifyMode(const std::string& name, VerifyMode* mode);   // 설정 문자열 → 모드 (그 외 false)

// UTF-16 → 표준 UTF-8. JNI 의 jstring 은 GetStringUTFChars(변형 UTF-8: 보조 평면 문자·NUL 의 바이트가 다름)
// 대신 GetStringRegion 으로 받은 UTF-16 을 이 함수로 변환해야 서명한 바이트와 같아진다.
// 짝 없는 서로게이트가 있으면 false.
bool Utf16ToUtf8(const std::uint16_t* utf16, std::size_t length, std::string* out);

// ---------------------------------------------------------------------------------------------
// (B) 파일 경로 + 값 정책 내장
// ---------------------------------------------------------------------------------------------

// 채널 1개 점유권. 소멸하거나 Release() 하면 반납된다. 이동만 가능.
// Manager보다 오래 살아도 안전하다(내부 상태를 공유 소유).
class Channel {
 public:
  Channel() = default;
  Channel(Channel&& other) noexcept;
  Channel& operator=(Channel&& other) noexcept;  // 기존 점유분은 먼저 반납
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;
  ~Channel();

  void Release() noexcept;  // 중복 호출 안전
  bool held() const noexcept { return shared_ != nullptr; }

 private:
  friend class Manager;
  std::shared_ptr<detail::Shared> shared_;
  Kind kind_ = Kind::Offline;
};

class Manager {
 public:
  // 배포 빌드용: 빌드 시 내장된 공개키로 검증한다.
  // 서명·형식 오류, NotYetValid, Expired 이면 실패한다(기동 게이트).
  static Error Open(const std::string& path, std::unique_ptr<Manager>* out);

  // 테스트·도구용: 내부 헤더(detail.h)의 Config로 공개키·시계·점검 주기를 지정한다.
  static Error OpenWith(const std::string& path, const detail::Config& config,
                        std::unique_ptr<Manager>* out);

  ~Manager();
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;

  // 채널 1개를 점유한다. 마지막 점검 후 점검 주기(기본 60초)가 지났으면 먼저 파일을 재적재·재판정한다.
  // Grace 상태에서도 Ok를 반환한다(경고는 Snapshot().state로 판단).
  Error Acquire(Kind kind, Channel* out);

  // 점검 주기와 무관하게 즉시 파일을 재적재한다. 실패하면 기존 라이선스를 유지하고 오류를 반환한다.
  Error Reload();

  // 현재 상태. Acquire와 같은 조건으로 지연 점검을 수행한다.
  Info Snapshot();

  // 현재 라이선스의 채널 한도 (세션 풀 크기 결정용).
  int MaxChannels(Kind kind);

 private:
  explicit Manager(std::shared_ptr<detail::Shared> shared);
  std::shared_ptr<detail::Shared> shared_;
};

}  // namespace license
}  // namespace stt

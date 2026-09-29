// stt_license — 폐쇄망 오프라인 라이선스 검증·집행 코어 (SDK 공개 헤더)
//
// 사용 흐름 (SDK):
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
namespace detail { struct Config; struct Shared; }

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

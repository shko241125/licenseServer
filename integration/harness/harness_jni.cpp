// JNI 통합 하네스: 실제 STT SDK(SonaSttAPI)의 호출 모델을 흉내 내어 라이선스 코어 결합 방식을 실행 검증한다.
//   connectSonaSTT    → connect(licensePath, sonasttSessions, whisperSessions)
//   getIdleSession    → getIdleSession(engine)        : 채널 점유 후 세션 예약
//   onResult/onError  → finishTask(sessionId)         : 세션 반환 + 채널 반납
//   getSTTSessionInfo → getSTTSessionInfo()           : 세션 + 라이선스 상태
// 실제 SDK에 적용할 때는 이 파일의 결합 지점(★)만 옮기면 된다. 세션·워커 구현은 SDK 것을 쓴다.
#include <jni.h>

#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "stt_license/license.h"

namespace {

namespace lic = stt::license;

// SDK 기존 오류 코드와 겹치지 않도록 라이선스 오류는 1000번대로 반환한다.
constexpr int kLicenseErrorBase = 1000;
constexpr int kErrInvalidArgument = 2;
constexpr int kErrNotConnected = 3;
constexpr int kErrNoIdleSession = 4;
constexpr int kErrInternal = 9;

struct Session {
  std::string engine;
  bool busy = false;
  lic::Channel channel;  // 예약~작업 종료 동안 점유
};

struct State {
  std::mutex mu;
  std::unique_ptr<lic::Manager> manager;
  std::map<std::string, Session> sessions;  // "sonastt-0", "whisper-3" ...
  lic::VerifiedLicense host_license;        // connectHostLicense 로 받은, 서명 검증된 라이선스
  lic::VerifyMode host_mode = lic::VerifyMode::SignatureOnly;
};

// ★ SDK 설정 파일(sonastt_service.cfg 형식 "--key=value", '#' 주석)에서 검증 수준을 읽는다.
//   키가 없으면 SignatureOnly(기존 SDK 동작 유지), 값이 잘못되면 false(연결 거부 — 오타로 검증이 약해지지 않게).
bool ReadVerifyMode(const std::string& config_file, lic::VerifyMode* mode) {
  *mode = lic::VerifyMode::SignatureOnly;
  std::ifstream f(config_file);
  if (!f) return true;  // 설정 파일이 없으면 기본값 (실제 SDK 는 자체 규칙을 따름)
  const std::string key = "--license-verify-mode=";
  std::string line;
  while (std::getline(f, line)) {
    line = line.substr(0, line.find('#'));
    size_t b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos) continue;
    line = line.substr(b, line.find_last_not_of(" \t\r") - b + 1);
    if (line.compare(0, key.size(), key) == 0) return lic::ParseVerifyMode(line.substr(key.size()), mode);
  }
  return true;
}

State& G() {
  static State s;
  return s;
}

int LicenseCode(lic::Error e) { return kLicenseErrorBase + static_cast<int>(e); }

std::string Quote(const std::string& s) {
  std::string out = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out + "\"";
}

std::string ErrorJson(int code, const char* name, const char* message) {
  return "{\"result\":" + std::to_string(code) + ",\"error\":" + Quote(name) +
         ",\"message\":" + Quote(message) + "}";
}

bool GetString(JNIEnv* env, jstring js, std::string* out) {
  if (js == nullptr) return false;
  const char* p = env->GetStringUTFChars(js, nullptr);
  if (p == nullptr) return false;  // OutOfMemoryError 가 이미 걸려 있음
  *out = p;
  env->ReleaseStringUTFChars(js, p);
  return true;
}

// ★ jstring → 표준 UTF-8. GetStringUTFChars 는 변형 UTF-8(보조 평면 문자·NUL 바이트가 다름)이라
//   서명한 바이트와 달라질 수 있으므로 UTF-16 으로 받아 코어의 Utf16ToUtf8 로 변환한다.
//   반환: Ok, FileTooLarge(길이 상한 초과), ParseError(짝 없는 서로게이트), Internal(JNI 오류).
lic::Error JStringToUtf8(JNIEnv* env, jstring js, std::string* out) {
  const jsize n = env->GetStringLength(js);
  if (n < 0) return lic::Error::Internal;
  if (static_cast<size_t>(n) > 64 * 1024) return lic::Error::FileTooLarge;  // UTF-8 로 바꾸기 전에 상한
  std::vector<jchar> buf(static_cast<size_t>(n));
  if (n > 0) env->GetStringRegion(js, 0, n, buf.data());
  if (env->ExceptionCheck()) return lic::Error::Internal;
  static_assert(sizeof(jchar) == sizeof(std::uint16_t), "jchar must be 16-bit");
  if (!lic::Utf16ToUtf8(reinterpret_cast<const std::uint16_t*>(buf.data()), buf.size(), out)) {
    return lic::Error::ParseError;
  }
  return lic::Error::Ok;
}

// C++ 예외가 JVM 으로 넘어가면 프로세스가 죽는다. 모든 진입점을 이것으로 감싼다.
template <typename F, typename R>
R Guard(F&& f, R on_error) {
  try {
    return f();
  } catch (...) {
    return on_error;
  }
}

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_licenseharness_SttHarness_connect(JNIEnv* env, jobject,
                                                               jstring jpath, jint sonastt,
                                                               jint whisper) {
  return Guard([&]() -> jint {
    std::string path;
    if (!GetString(env, jpath, &path) || sonastt < 0 || whisper < 0) return kErrInvalidArgument;
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    // ★ 기동 게이트: 라이선스가 유효하지 않으면 SDK 초기화 자체를 실패시킨다.
    std::unique_ptr<lic::Manager> m;
    lic::Error e = lic::Manager::Open(path, &m);
    if (e != lic::Error::Ok) return LicenseCode(e);
    // ★ 세션 풀 크기: 엔진별 설정값과 라이선스 한도 중 작은 값 (한도를 넘는 GPU 메모리 할당 방지).
    //   동시 사용 총량은 Acquire 가 offline_stt 로 따로 강제한다.
    const int cap = m->MaxChannels(lic::Kind::Offline);
    g.sessions.clear();
    for (int i = 0; i < std::min<int>(sonastt, cap); ++i) g.sessions["sonastt-" + std::to_string(i)].engine = "sonastt";
    for (int i = 0; i < std::min<int>(whisper, cap); ++i) g.sessions["whisper-" + std::to_string(i)].engine = "whisper";
    g.manager = std::move(m);
    return 0;
  }, jint{kErrInternal});
}

JNIEXPORT jstring JNICALL Java_licenseharness_SttHarness_getIdleSession(JNIEnv* env, jobject,
                                                                        jstring jengine) {
  std::string out = Guard([&]() -> std::string {
    std::string engine;
    if (!GetString(env, jengine, &engine)) {
      return ErrorJson(kErrInvalidArgument, "INVALID_ARGUMENT", "engine is null");
    }
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    if (!g.manager) return ErrorJson(kErrNotConnected, "NOT_CONNECTED", "call connect first");
    for (auto& [id, s] : g.sessions) {
      if (s.engine != engine || s.busy) continue;
      // ★ 채널 점유: 두 엔진 합계가 offline_stt 를 넘지 않게 한다. 만료·미개시도 여기서 거부된다.
      lic::Error e = g.manager->Acquire(lic::Kind::Offline, &s.channel);
      if (e != lic::Error::Ok) {
        return ErrorJson(LicenseCode(e), lic::ErrorName(e), lic::ErrorMessage(e));
      }
      s.busy = true;
      return "{\"result\":0,\"session_id\":" + Quote(id) + "}";
    }
    return ErrorJson(kErrNoIdleSession, "NO_IDLE_SESSION", "all sessions of this engine are busy");
  }, ErrorJson(kErrInternal, "INTERNAL", "internal error"));
  return env->NewStringUTF(out.c_str());
}

JNIEXPORT jint JNICALL Java_licenseharness_SttHarness_finishTask(JNIEnv* env, jobject,
                                                                  jstring jid) {
  return Guard([&]() -> jint {
    std::string id;
    if (!GetString(env, jid, &id)) return kErrInvalidArgument;
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    auto it = g.sessions.find(id);
    if (it == g.sessions.end() || !it->second.busy) return kErrInvalidArgument;
    it->second.busy = false;
    it->second.channel.Release();  // ★ 작업 종료(onResult/onError/abortTask) 시 반납
    return 0;
  }, jint{kErrInternal});
}

JNIEXPORT jstring JNICALL Java_licenseharness_SttHarness_getSTTSessionInfo(JNIEnv* env, jobject) {
  std::string out = Guard([&]() -> std::string {
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    if (!g.manager) return ErrorJson(kErrNotConnected, "NOT_CONNECTED", "call connect first");
    int total = 0, busy = 0;
    for (const auto& kv : g.sessions) {
      ++total;
      busy += kv.second.busy ? 1 : 0;
    }
    // ★ 라이선스 상태를 기존 세션 정보에 덧붙인다 (서버의 health/로그가 이 값을 읽음).
    return "{\"result\":0,\"sessions_total\":" + std::to_string(total) +
           ",\"sessions_busy\":" + std::to_string(busy) +
           ",\"license\":" + lic::InfoToJson(g.manager->Snapshot()) + "}";
  }, ErrorJson(kErrInternal, "INTERNAL", "internal error"));
  return env->NewStringUTF(out.c_str());
}

JNIEXPORT jint JNICALL Java_licenseharness_SttHarness_reloadLicense(JNIEnv*, jobject) {
  return Guard([&]() -> jint {
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    if (!g.manager) return kErrNotConnected;
    lic::Error e = g.manager->Reload();
    return e == lic::Error::Ok ? 0 : LicenseCode(e);
  }, jint{kErrInternal});
}

// ---- 실제 STT 서버 API 형태: connect(SonaSttListener callback, String sttHomePath, String hostLicense) ----
// 공통 코어는 서명만 검증하고, 어떤 값을 쓸지(채널·기간 등)는 SDK 가 정한다.
JNIEXPORT jint JNICALL Java_licenseharness_SttHarness_connectHostLicense(JNIEnv* env, jobject, jobject /*callback*/,
                                                                          jstring configFile,
                                                                          jstring hostLicense) {
  return Guard([&]() -> jint {
    if (hostLicense == nullptr) return kErrInvalidArgument;
    std::string cfg;
    lic::VerifyMode mode;
    if (configFile != nullptr && !GetString(env, configFile, &cfg)) return kErrInvalidArgument;  // 경로는 ASCII 전제
    if (!ReadVerifyMode(cfg, &mode)) return kErrInvalidArgument;                                 // 잘못된 모드 값
    std::string text;
    lic::Error e = JStringToUtf8(env, hostLicense, &text);
    lic::VerifiedLicense verified;
    lic::LicenseFields fields;
    std::string detail;
    // ★ 공통 진입점: SignatureOnly = 서명만 / Full = 서명 + 표준 필드 + 기간 (분리 함수 VerifySignature·VerifyLicense 와 동일)
    if (e == lic::Error::Ok) e = lic::Verify(text, mode, &verified, &fields, &detail);
    if (e != lic::Error::Ok) return LicenseCode(e);                               // 검증 실패 → 연결 거부
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    g.host_license = verified;
    g.host_mode = mode;
    // ★ 여기부터는 SDK 정책 영역: 예) allowed_channels 가 있으면 세션 수 상한으로 쓰고, 없으면 기존 설정 유지.
    //    기간 검사를 할지(ParseStandardFields + StateAt), 무시할지도 SDK 가 정한다.
    return 0;
  }, jint{kErrInternal});
}

// SDK 가 검증된 값을 어떻게 꺼내는지 보여 주는 조회용 (테스트에서 확인).
JNIEXPORT jstring JNICALL Java_licenseharness_SttHarness_hostLicenseInfo(JNIEnv* env, jobject) {
  std::string out = Guard([&]() -> std::string {
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    const lic::VerifiedLicense& v = g.host_license;
    if (!v.valid()) return ErrorJson(kErrNotConnected, "NOT_CONNECTED", "no verified host license");
    std::int64_t offline = -1, online = -1;
    std::string site, not_after, project;
    v.GetInt("allowed_channels.offline_stt", &offline);   // 없으면 -1 (SDK 기본값 사용 신호)
    v.GetInt("allowed_channels.online_stt", &online);
    v.GetString("site_id", &site);
    v.GetString("validity.not_after", &not_after);
    v.GetString("project_name", &project);
    lic::LicenseFields f;
    lic::Error std_e = lic::ParseStandardFields(v, &f);    // 선택: 표준 규칙 판정 결과만 알려 줌
    return "{\"result\":0,\"offline_stt\":" + std::to_string(offline) + ",\"online_stt\":" + std::to_string(online) +
           ",\"site_id\":" + Quote(site) + ",\"not_after\":" + Quote(not_after) +
           ",\"project_name\":" + Quote(project) + ",\"standard\":" + Quote(lic::ErrorName(std_e)) +
           ",\"mode\":" + Quote(lic::VerifyModeName(g.host_mode)) +
           ",\"canonical_bytes\":" + std::to_string(v.canonical_json().size()) + "}";
  }, ErrorJson(kErrInternal, "INTERNAL", "internal error"));
  // 반환 문자열에 비 ASCII(한글 등)가 있을 수 있어 NewStringUTF(변형 UTF-8 입력) 대신 UTF-16 으로 만든다.
  std::u16string u16;
  for (size_t i = 0; i < out.size();) {
    unsigned char c = static_cast<unsigned char>(out[i]);
    int len = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4;
    uint32_t cp = len == 1 ? c : len == 2 ? (c & 0x1F) : len == 3 ? (c & 0x0F) : (c & 0x07);
    for (int k = 1; k < len && i + k < out.size(); ++k) cp = (cp << 6) | (static_cast<unsigned char>(out[i + k]) & 0x3F);
    i += static_cast<size_t>(len);
    if (cp >= 0x10000) {
      cp -= 0x10000;
      u16.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      u16.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      u16.push_back(static_cast<char16_t>(cp));
    }
  }
  return env->NewString(reinterpret_cast<const jchar*>(u16.data()), static_cast<jsize>(u16.size()));
}

// 대조군: GetStringUTFChars(변형 UTF-8)로 받아 검증하면 보조 평면 문자가 있는 정상 라이선스가 실패함을 보인다.
// 실제 SDK 에서 쓰면 안 되는 방식이다.
JNIEXPORT jint JNICALL Java_licenseharness_SttHarness_verifyViaModifiedUtf8(JNIEnv* env, jobject, jstring s) {
  return Guard([&]() -> jint {
    std::string text;
    if (!GetString(env, s, &text)) return kErrInvalidArgument;  // GetStringUTFChars
    lic::VerifiedLicense v;
    lic::Error e = lic::VerifySignature(text, &v);
    return e == lic::Error::Ok ? 0 : LicenseCode(e);
  }, jint{kErrInternal});
}

JNIEXPORT void JNICALL Java_licenseharness_SttHarness_disconnect(JNIEnv*, jobject) {
  Guard([&]() -> int {
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    g.sessions.clear();  // Channel 소멸 → 반납
    g.manager.reset();
    g.host_license = lic::VerifiedLicense();
    return 0;
  }, 0);
}

}  // extern "C"

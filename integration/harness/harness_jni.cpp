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
};

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

JNIEXPORT void JNICALL Java_licenseharness_SttHarness_disconnect(JNIEnv*, jobject) {
  Guard([&]() -> int {
    State& g = G();
    std::lock_guard<std::mutex> lock(g.mu);
    g.sessions.clear();  // Channel 소멸 → 반납
    g.manager.reset();
    return 0;
  }, 0);
}

}  // extern "C"

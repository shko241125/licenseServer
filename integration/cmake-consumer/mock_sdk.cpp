// 부모 프로젝트의 C++ 표준(기본 C++14)으로 공개 헤더를 포함할 수 있는지 확인한다.
#include <memory>

#include "stt_license/license.h"

namespace {
std::unique_ptr<stt::license::Manager> g_manager;
}

extern "C" __attribute__((visibility("default"))) int mock_connect(const char* path) {
  stt::license::Error e = stt::license::Manager::Open(path, &g_manager);
  return e == stt::license::Error::Ok ? 0 : 1000 + static_cast<int>(e);
}

extern "C" __attribute__((visibility("default"))) int mock_acquire_release() {
  if (!g_manager) return -1;
  stt::license::Channel ch;
  stt::license::Error e = g_manager->Acquire(stt::license::Kind::Offline, &ch);
  return e == stt::license::Error::Ok ? 0 : 1000 + static_cast<int>(e);
}

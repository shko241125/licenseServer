#include <gtest/gtest.h>
#include <sys/stat.h>

#include <atomic>
#include <thread>
#include <vector>

#include "test_util.h"

namespace {

using namespace testutil;
using lic::Channel;
using lic::Error;
using lic::Manager;
using lic::State;

// 기본 계약(Contract::Dated, 개발): total 16, offline sonastt 8 / sonaml 4 / sonaasr 4, online sonastt 8 / sonaasr 2,
// 2026-09-21 ~ 2027-09-20T23:59:59Z
class ManagerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    now_ = T("2026-10-01T00:00:00Z");
    path_ = dir_.File("license.lic");
  }

  lic::detail::Config Cfg() {
    lic::detail::Config cfg;
    cfg.public_key = PublicOf(TestSeed());
    cfg.now = [this] { return now_.load(); };
    cfg.recheck_seconds = 60;
    return cfg;
  }

  Error Open(std::unique_ptr<Manager>* m) { return Manager::OpenWith(path_, Cfg(), m); }

  std::unique_ptr<Manager> MustOpen(const Contract& c = Contract::Dated()) {
    ReplaceFile(path_, Issue(c));
    std::unique_ptr<Manager> m;
    EXPECT_EQ(Open(&m), Error::Ok);
    return m;
  }

  static int Used(Manager& m, const std::string& k, const std::string& engine) {
    for (const lic::ChannelUsage& u : m.Snapshot().engines) {
      if (u.group == k && u.engine == engine) return u.used;
    }
    return 0;
  }

  TempDir dir_;
  std::string path_;
  std::atomic<int64_t> now_{0};
};

TEST_F(ManagerTest, OpenFailures) {
  std::unique_ptr<Manager> m;
  EXPECT_EQ(Open(&m), Error::FileNotFound);

  ReplaceFile(path_, Replace(Issue(Contract::Dated()), "\"sonastt\": 8", "\"sonastt\": 16"));
  EXPECT_EQ(Open(&m), Error::BadSignature);

  ReplaceFile(path_, "not json");
  EXPECT_EQ(Open(&m), Error::ParseError);

  Contract future = Contract::Dated();
  future.not_before = "2027-01-01T00:00:00Z";
  ReplaceFile(path_, Issue(future));
  EXPECT_EQ(Open(&m), Error::NotYetValid);

  Contract old = Contract::Dated("2025-12-31T23:59:59Z");
  old.not_before = "2025-01-01T00:00:00Z";
  ReplaceFile(path_, Issue(old));
  EXPECT_EQ(Open(&m), Error::Expired);
  EXPECT_EQ(m, nullptr);

  // license_ver 없는 파일 / 지원하지 않는 license_ver
  ReplaceFile(path_, SignRaw(Replace(Contract::Dated().Json(), "\"license_ver\":1,", "")));
  EXPECT_EQ(Open(&m), Error::InvalidField);
  ReplaceFile(path_, SignRaw(Replace(Contract::Dated().Json(), "\"license_ver\":1,", "\"license_ver\":2,")));
  EXPECT_EQ(Open(&m), Error::UnsupportedVersion);

  // 일반 파일이 아닌 경로: 디렉터리, FIFO(쓰는 쪽이 없으면 예전 구현은 무기한 대기)
  EXPECT_EQ(Manager::OpenWith(std::filesystem::path(path_).parent_path().string(), Cfg(), &m),
            Error::FileNotFound);
  const std::string fifo = dir_.File("license.fifo");
  ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);
  EXPECT_EQ(Manager::OpenWith(fifo, Cfg(), &m), Error::FileNotFound);

  // 다른 키로 서명된 정상 형식 파일
  ReplaceFile(path_, Issue(Contract::Dated(), TestSeed(50)));
  EXPECT_EQ(Open(&m), Error::BadSignature);
  ReplaceFile(path_, Issue(Contract::Dated()));
  EXPECT_EQ(Open(&m), Error::Ok);
  ASSERT_NE(m, nullptr);
}

TEST_F(ManagerTest, EngineLimitThenRelease) {
  auto m = MustOpen();
  EXPECT_EQ(m->MaxChannels("offline", "sonaml"), 4);
  EXPECT_EQ(m->MaxChannels("online", "sonastt"), 8);
  EXPECT_EQ(m->MaxChannels("online", "sonaml"), 0);  // 온라인에는 없는 엔진
  EXPECT_EQ(m->TotalChannels(), 16);
  std::vector<Channel> held(4);
  for (auto& ch : held) ASSERT_EQ(m->Acquire("offline", "sonaml", &ch), Error::Ok);
  Channel extra;
  EXPECT_EQ(m->Acquire("offline", "sonaml", &extra), Error::ChannelLimit);
  EXPECT_FALSE(extra.held());
  // 엔진별 독립 집계: 같은 이름이라도 온라인/오프라인은 다른 엔진
  EXPECT_EQ(m->Acquire("offline", "sonastt", &extra), Error::Ok);
  EXPECT_EQ(Used(*m, "offline", "sonastt"), 1);
  EXPECT_EQ(Used(*m, "online", "sonastt"), 0);
  extra.Release();
  extra.Release();  // 중복 반납 무해
  EXPECT_EQ(Used(*m, "offline", "sonastt"), 0);

  held[3].Release();
  EXPECT_EQ(Used(*m, "offline", "sonaml"), 3);
  EXPECT_EQ(m->Snapshot().total_used, 3);
  EXPECT_EQ(m->Acquire("offline", "sonaml", &extra), Error::Ok);
  EXPECT_EQ(m->Acquire("offline", "sonaml", &held[3]), Error::ChannelLimit);
  held.clear();  // 소멸자 반납
  extra.Release();
  EXPECT_EQ(m->Snapshot().total_used, 0);
}

TEST_F(ManagerTest, EngineNotLicensed) {
  auto m = MustOpen();
  Channel ch;
  EXPECT_EQ(m->Acquire("online", "sonaml", &ch), Error::EngineNotLicensed);   // 온라인 sonaml 없음
  EXPECT_EQ(m->Acquire("offline", "whisper", &ch), Error::EngineNotLicensed);
  EXPECT_EQ(m->Acquire("offline", "", &ch), Error::EngineNotLicensed);
  EXPECT_FALSE(ch.held());
  EXPECT_STREQ(lic::ErrorName(Error::EngineNotLicensed), "LICENSE_ENGINE_NOT_LICENSED");
  EXPECT_EQ(static_cast<int>(Error::EngineNotLicensed), 11);  // JNI 코드 1011

  Contract zero = Contract::Dated();
  zero.offline = {{"sonastt", 8}, {"sonaml", 0}};  // 상한 0 = 사용 안 함
  auto z = MustOpen(zero);
  EXPECT_EQ(z->Acquire("offline", "sonaml", &ch), Error::EngineNotLicensed);
}

TEST_F(ManagerTest, CustomGroups) {
  // 항목 이름은 자유: offline·online 이 없어도 되고, 같은 세부 항목 이름이 항목마다 따로 집계된다.
  Contract c;
  c.total = 5;
  c.offline = {};
  c.online = {};
  c.extra = {{"streaming", {{"sonastt", 3}}}, {"batch_gpu", {{"sonastt", 4}, {"sonaml", 1}}}};
  auto m = MustOpen(c);
  EXPECT_EQ(m->MaxChannels("streaming", "sonastt"), 3);
  EXPECT_EQ(m->MaxChannels("batch_gpu", "sonastt"), 4);
  EXPECT_EQ(m->MaxChannels("offline", "sonastt"), 0);
  EXPECT_EQ(m->TotalChannels(), 5);
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::EngineNotLicensed);
  EXPECT_EQ(m->Acquire("total", "sonastt", &ch), Error::EngineNotLicensed);  // total 은 항목이 아님
  std::vector<Channel> held(5);
  for (int i = 0; i < 3; ++i) ASSERT_EQ(m->Acquire("streaming", "sonastt", &held[i]), Error::Ok);
  EXPECT_EQ(m->Acquire("streaming", "sonastt", &ch), Error::ChannelLimit);   // 항목 상한
  ASSERT_EQ(m->Acquire("batch_gpu", "sonastt", &held[3]), Error::Ok);        // 다른 항목의 같은 이름
  ASSERT_EQ(m->Acquire("batch_gpu", "sonaml", &held[4]), Error::Ok);
  EXPECT_EQ(m->Acquire("batch_gpu", "sonastt", &ch), Error::ChannelLimit);   // total 5 소진
  EXPECT_EQ(Used(*m, "streaming", "sonastt"), 3);
  EXPECT_EQ(Used(*m, "batch_gpu", "sonastt"), 1);
  const std::string j = lic::InfoToJson(m->Snapshot());
  EXPECT_NE(j.find("\"batch_gpu\":{\"sonaml\":{\"max\":1,\"used\":1},\"sonastt\":{\"max\":4,\"used\":1}}"),
            std::string::npos) << j;
  EXPECT_NE(j.find("\"streaming\":{\"sonastt\":{\"max\":3,\"used\":3}}"), std::string::npos) << j;
  EXPECT_NE(j.find("\"total\":{\"max\":5,\"used\":5}"), std::string::npos) << j;
  EXPECT_EQ(j.find("\"offline\""), std::string::npos) << j;
}

TEST_F(ManagerTest, TotalLimitAcrossOnlineAndOffline) {
  // total 16 < 엔진 합(오프라인 16 + 온라인 10 = 26): 어느 조합이든 동시에 16개까지만
  auto m = MustOpen();
  std::vector<Channel> held;
  auto take = [&](const char* k, const char* e, int n) {
    for (int i = 0; i < n; ++i) {
      held.emplace_back();
      ASSERT_EQ(m->Acquire(k, e, &held.back()), Error::Ok) << e << " #" << i;
    }
  };
  take("offline", "sonastt", 8);
  take("online", "sonastt", 6);
  take("online", "sonaasr", 2);
  EXPECT_EQ(m->Snapshot().total_used, 16);
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonaml", &ch), Error::ChannelLimit);   // sonaml 은 비었지만 total 소진
  EXPECT_EQ(m->Acquire("online", "sonastt", &ch), Error::ChannelLimit);
  held.pop_back();                                                             // 온라인 sonaasr 1개 반납
  EXPECT_EQ(m->Acquire("offline", "sonaml", &ch), Error::Ok);
  EXPECT_EQ(m->Snapshot().total_used, 16);
}

TEST_F(ManagerTest, ChannelMoveSemantics) {
  auto m = MustOpen();
  Channel a;
  ASSERT_EQ(m->Acquire("offline", "sonastt", &a), Error::Ok);
  Channel b(std::move(a));
  EXPECT_FALSE(a.held());
  EXPECT_TRUE(b.held());
  EXPECT_EQ(Used(*m, "offline", "sonastt"), 1);
  Channel c;
  ASSERT_EQ(m->Acquire("online", "sonaasr", &c), Error::Ok);
  EXPECT_EQ(m->Snapshot().total_used, 2);
  c = std::move(b);  // c의 기존 점유분(온라인 sonaasr) 반납 후 b 인수
  EXPECT_EQ(m->Snapshot().total_used, 1);
  EXPECT_EQ(Used(*m, "online", "sonaasr"), 0);
  EXPECT_EQ(Used(*m, "offline", "sonastt"), 1);
  Channel& same = c;  // GCC 13+ -Wself-move 를 피하면서 자기 이동 대입을 검사
  c = std::move(same);  // 자기 대입 무해
  EXPECT_EQ(m->Snapshot().total_used, 1);
  // 이미 채널을 가진 out 으로 Acquire: 기존 반납 후 새로 점유 (교착 없음)
  ASSERT_EQ(m->Acquire("offline", "sonaml", &c), Error::Ok);
  EXPECT_EQ(m->Snapshot().total_used, 1);
  EXPECT_EQ(Used(*m, "offline", "sonastt"), 0);
  EXPECT_EQ(Used(*m, "offline", "sonaml"), 1);
}

TEST_F(ManagerTest, ChannelOutlivesManager) {
  Channel ch;
  {
    auto m = MustOpen();
    ASSERT_EQ(m->Acquire("online", "sonastt", &ch), Error::Ok);
  }
  EXPECT_TRUE(ch.held());
  ch.Release();  // 해제된 Manager 메모리를 건드리지 않아야 함 (ASan 으로 확인)
  EXPECT_FALSE(ch.held());
}

TEST_F(ManagerTest, ExpiresRightAfterNotAfter) {
  auto m = MustOpen();
  Channel running;
  ASSERT_EQ(m->Acquire("offline", "sonastt", &running), Error::Ok);

  now_ = T("2027-09-20T23:59:59Z");  // 종료 순간까지 유효
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::Ok);
  EXPECT_EQ(m->Snapshot().state, State::Valid);
  ch.Release();

  now_ = T("2027-09-21T00:00:00Z");  // 1초 뒤 만료 (유예 없음)
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::Expired);
  lic::Info info = m->Snapshot();
  EXPECT_EQ(info.state, State::Expired);
  EXPECT_EQ(info.total_used, 1);  // 진행 중 세션은 유지
  running.Release();
  EXPECT_EQ(m->Snapshot().total_used, 0);
}

TEST_F(ManagerTest, PermanentLicenseStaysValid) {
  auto m = MustOpen(Contract{});  // 운영: 9999-12-31T23:59:59Z
  now_ = T("2200-01-01T00:00:00Z");
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::Ok);
  EXPECT_EQ(m->Snapshot().state, State::Valid);
}

TEST_F(ManagerTest, LazyReloadAfterInterval) {
  auto m = MustOpen();
  Contract more = Contract::Dated();
  more.total = 30;
  more.offline = {{"sonastt", 20}};
  more.license_id = "11111111111111111111111111111111";
  ReplaceFile(path_, Issue(more));

  now_ += 59;
  EXPECT_EQ(m->MaxChannels("offline", "sonastt"), 8);  // 아직 점검 주기 전
  now_ += 1;
  EXPECT_EQ(m->MaxChannels("offline", "sonastt"), 20);
  EXPECT_EQ(m->MaxChannels("offline", "sonaml"), 0);    // 새 라이선스에서 빠진 엔진
  EXPECT_EQ(m->TotalChannels(), 30);
  EXPECT_EQ(m->Snapshot().license_id, more.license_id);
  EXPECT_EQ(m->Snapshot().last_reload_error, Error::Ok);
}

TEST_F(ManagerTest, ExplicitReloadIsImmediate) {
  auto m = MustOpen();
  Contract fewer = Contract::Dated();
  fewer.total = 3;
  fewer.offline = {{"sonastt", 3}};
  fewer.online = {};
  ReplaceFile(path_, Issue(fewer));
  EXPECT_EQ(m->Reload(), Error::Ok);
  EXPECT_EQ(m->MaxChannels("offline", "sonastt"), 3);
  EXPECT_EQ(m->TotalChannels(), 3);
}

TEST_F(ManagerTest, BadReplacementKeepsCurrentLicense) {
  auto m = MustOpen();
  const std::string good = Issue(Contract::Dated());

  ReplaceFile(path_, Replace(good, "\"total\": 16", "\"total\": 800"));
  EXPECT_EQ(m->Reload(), Error::BadSignature);
  lic::Info info = m->Snapshot();
  EXPECT_EQ(info.total_max, 16);
  EXPECT_EQ(info.state, State::Valid);
  EXPECT_EQ(info.last_reload_error, Error::BadSignature);
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::Ok);  // 서비스 계속
  ch.Release();

  std::filesystem::remove(path_);
  now_ += 60;
  EXPECT_EQ(m->Snapshot().last_reload_error, Error::FileNotFound);
  EXPECT_EQ(m->MaxChannels("offline", "sonastt"), 8);

  // 만료된 라이선스로의 교체 거부
  Contract old = Contract::Dated("2025-12-31T23:59:59Z");
  old.not_before = "2025-01-01T00:00:00Z";
  ReplaceFile(path_, Issue(old));
  EXPECT_EQ(m->Reload(), Error::Expired);
  EXPECT_EQ(m->Snapshot().state, State::Valid);

  // license_ver 없는 파일로의 교체 거부
  ReplaceFile(path_, SignRaw(Replace(Contract::Dated().Json(), "\"license_ver\":1,", "")));
  EXPECT_EQ(m->Reload(), Error::InvalidField);
  EXPECT_EQ(m->TotalChannels(), 16);

  // 원본 복구 → 오류 해제
  ReplaceFile(path_, good);
  now_ += 60;
  EXPECT_EQ(m->Snapshot().last_reload_error, Error::Ok);
}

TEST_F(ManagerTest, EarlyRenewalAppliesWhenItBecomesValid) {
  Contract cur = Contract::Dated("2026-12-31T23:59:59Z");
  auto m = MustOpen(cur);
  Contract renewal = Contract::Dated("2027-12-31T23:59:59Z");
  renewal.license_id = "22222222222222222222222222222222";
  renewal.not_before = "2027-01-01T00:00:00Z";
  ReplaceFile(path_, Issue(renewal));  // 미개시 갱신본을 미리 배치

  now_ += 60;
  lic::Info info = m->Snapshot();
  EXPECT_EQ(info.license_id, cur.license_id);  // 아직 기존 것 사용
  EXPECT_EQ(info.last_reload_error, Error::NotYetValid);

  now_ = T("2027-01-01T00:00:30Z");  // 기존 만료 후 + 갱신본 개시
  info = m->Snapshot();
  EXPECT_EQ(info.license_id, renewal.license_id);
  EXPECT_EQ(info.state, State::Valid);
  EXPECT_EQ(info.last_reload_error, Error::Ok);
}

TEST_F(ManagerTest, LimitReductionDoesNotEvict) {
  auto m = MustOpen();
  std::vector<Channel> held(6);
  for (auto& ch : held) ASSERT_EQ(m->Acquire("offline", "sonastt", &ch), Error::Ok);
  Contract fewer = Contract::Dated();
  fewer.offline = {{"sonastt", 2}};
  ReplaceFile(path_, Issue(fewer));
  ASSERT_EQ(m->Reload(), Error::Ok);
  EXPECT_EQ(Used(*m, "offline", "sonastt"), 6);  // 끊지 않음
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::ChannelLimit);
  held.resize(2);  // 4개 반납 → used 2
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::ChannelLimit);
  held.resize(1);
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::Ok);

  // 교체로 빠진 엔진도 사용 중이면 현황에 남는다(max 0)
  Channel ml;
  Contract base = Contract::Dated();
  ReplaceFile(path_, Issue(base));
  ASSERT_EQ(m->Reload(), Error::Ok);
  ASSERT_EQ(m->Acquire("offline", "sonaml", &ml), Error::Ok);
  ReplaceFile(path_, Issue(fewer));
  ASSERT_EQ(m->Reload(), Error::Ok);
  bool found = false;
  for (const lic::ChannelUsage& u : m->Snapshot().engines) {
    if (u.group == "offline" && u.engine == "sonaml") {
      found = true;
      EXPECT_EQ(u.max, 0);
      EXPECT_EQ(u.used, 1);
    }
  }
  EXPECT_TRUE(found);
  ml.Release();
  for (const lic::ChannelUsage& u : m->Snapshot().engines) EXPECT_NE(u.engine, "sonaml");
}

TEST_F(ManagerTest, ReplacedByFifoDoesNotBlock) {
  auto m = MustOpen();
  std::filesystem::remove(path_);
  ASSERT_EQ(::mkfifo(path_.c_str(), 0600), 0);
  EXPECT_EQ(m->Reload(), Error::FileNotFound);  // 멈추지 않고 즉시 실패
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::Ok);  // 기존 라이선스로 계속 서비스
}

TEST_F(ManagerTest, ClockRollbackTriggersRecheck) {
  auto m = MustOpen();
  Contract more = Contract::Dated();
  more.offline = {{"sonastt", 9}};
  ReplaceFile(path_, Issue(more));
  now_ -= 3600;  // 시계를 과거로
  EXPECT_EQ(m->MaxChannels("offline", "sonastt"), 9);
}

TEST_F(ManagerTest, InfoJson) {
  auto m = MustOpen();
  Channel ch;
  ASSERT_EQ(m->Acquire("online", "sonaasr", &ch), Error::Ok);
  EXPECT_EQ(lic::InfoToJson(m->Snapshot()),
            "{\"channels\":{\"offline\":{\"sonaasr\":{\"max\":4,\"used\":0},\"sonaml\":{\"max\":4,\"used\":0},"
            "\"sonastt\":{\"max\":8,\"used\":0}},\"online\":{\"sonaasr\":{\"max\":2,\"used\":1},"
            "\"sonastt\":{\"max\":8,\"used\":0}},\"total\":{\"max\":16,\"used\":1}},\"check_mode\":\"full\","
            "\"checked_at\":\"2026-10-01T00:00:00Z\",\"issued_at\":\"2026-09-21T00:00:00Z\","
            "\"last_reload_error\":\"OK\",\"license_id\":\"0123456789abcdef0123456789abcdef\","
            "\"license_type\":\"dev\",\"not_after\":\"2027-09-20T23:59:59Z\","
            "\"not_before\":\"2026-09-21T00:00:00Z\",\"project_name\":\"STT Platform "
            "Modernization\",\"site_id\":\"SEOUL-MAIN-DC\",\"solution\":\"sonastt-universal\","
            "\"state\":\"VALID\"}");
}

TEST_F(ManagerTest, ConcurrentAcquireNeverExceedsLimits) {
  // 엔진 상한 3·3, total 5: 두 엔진을 섞어 경쟁시켜도 엔진별 ≤ 3, 합 ≤ 5
  Contract c = Contract::Dated();
  c.total = 5;
  c.offline = {{"sonastt", 3}, {"sonaml", 3}};
  c.online = {};
  auto m = MustOpen(c);
  std::atomic<int> inside{0}, peak{0}, granted{0}, denied{0};
  std::atomic<int> in_e[2] = {{0}, {0}}, peak_e[2] = {{0}, {0}};
  std::vector<std::thread> threads;
  for (int t = 0; t < 16; ++t) {
    threads.emplace_back([&, t] {
      const int e = t % 2;
      const char* engine = e == 0 ? "sonastt" : "sonaml";
      for (int i = 0; i < 2000; ++i) {
        Channel ch;
        if (m->Acquire("offline", engine, &ch) != Error::Ok) { ++denied; continue; }
        ++granted;
        int now_inside = ++inside, now_e = ++in_e[e];
        int p = peak.load();
        while (now_inside > p && !peak.compare_exchange_weak(p, now_inside)) {}
        int pe = peak_e[e].load();
        while (now_e > pe && !peak_e[e].compare_exchange_weak(pe, now_e)) {}
        --in_e[e];
        --inside;
      }  // ch 소멸 → 반납
    });
  }
  for (auto& th : threads) th.join();
  EXPECT_LE(peak.load(), 5);
  EXPECT_LE(peak_e[0].load(), 3);
  EXPECT_LE(peak_e[1].load(), 3);
  EXPECT_GT(granted.load(), 0);
  EXPECT_EQ(granted + denied, 16 * 2000);
  EXPECT_EQ(m->Snapshot().total_used, 0);
}

TEST_F(ManagerTest, ConcurrentReloadAndAcquire) {
  auto m = MustOpen();
  Contract a = Contract::Dated(), b = Contract::Dated();
  b.total = 4;
  b.offline = {{"sonastt", 4}};
  b.online = {{"sonaasr", 1}};
  const std::string ta = Issue(a), tb = Issue(b);
  std::atomic<bool> stop{false};
  std::thread writer([&] {
    for (int i = 0; i < 200; ++i) {
      ReplaceFile(path_, i % 2 ? ta : tb);
      m->Reload();
    }
    stop = true;
  });
  std::vector<std::thread> users;
  for (int t = 0; t < 4; ++t) {
    users.emplace_back([&, t] {
      while (!stop) {
        Channel ch;
        m->Acquire(t % 2 ? "offline" : "online", t % 2 ? "sonaml" : "sonaasr", &ch);
        m->Snapshot();
      }
    });
  }
  writer.join();
  for (auto& u : users) u.join();
  EXPECT_EQ(m->Snapshot().total_used, 0);
  EXPECT_TRUE(m->Snapshot().engines.size() > 0);
}

}  // namespace

namespace {

// check_mode "minimal" + 모르는 판(2): 운영 중 full 파일에서 교체해도 안정 필드로 집행하고, 표시 필드는 비어 있을 수 있다.
TEST_F(ManagerTest, ReloadToMinimalFileOfUnknownVersion) {
  std::unique_ptr<Manager> m = MustOpen();
  EXPECT_EQ(m->Snapshot().check_mode, lic::CheckMode::Full);
  std::string json = Replace(Contract::Dated().Json(), "\"license_ver\":1,", "\"check_mode\":\"minimal\",\"license_ver\":2,");
  json = Replace(json, "\"issued_at\":\"2026-09-21T00:00:00Z\",", "");
  json = Replace(json, "\"site_id\":\"SEOUL-MAIN-DC\"", "\"site_id\":123");
  json = Replace(json, "\"sonaml\":4,", "");
  ReplaceFile(path_, SignRaw(json));
  ASSERT_EQ(m->Reload(), Error::Ok);
  lic::Info i = m->Snapshot();
  EXPECT_EQ(i.check_mode, lic::CheckMode::Minimal);
  EXPECT_EQ(i.site_id, "");
  EXPECT_EQ(i.issued_at, 0);
  const std::string js = lic::InfoToJson(i);
  EXPECT_NE(js.find("\"check_mode\":\"minimal\""), std::string::npos) << js;
  EXPECT_NE(js.find("\"issued_at\":\"\""), std::string::npos) << js;   // 1970 으로 보이지 않음
  EXPECT_NE(js.find("\"not_after\":\"2027-09-20T23:59:59Z\""), std::string::npos) << js;
  Channel ch;
  EXPECT_EQ(m->Acquire("offline", "sonaml", &ch), Error::EngineNotLicensed);  // M18
  std::vector<Channel> held(8);
  for (auto& c : held) ASSERT_EQ(m->Acquire("offline", "sonastt", &c), Error::Ok);
  EXPECT_EQ(m->Acquire("offline", "sonastt", &ch), Error::ChannelLimit);      // M19
  now_ = T("2027-09-21T00:00:00Z");
  EXPECT_EQ(m->Acquire("offline", "sonaasr", &ch), Error::Expired);           // M20
}

}  // namespace

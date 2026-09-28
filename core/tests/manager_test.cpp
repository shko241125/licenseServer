#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "test_util.h"

namespace {

using namespace testutil;
using lic::Channel;
using lic::Error;
using lic::Kind;
using lic::Manager;
using lic::State;

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

  std::unique_ptr<Manager> MustOpen(const Contract& c = Contract{}) {
    ReplaceFile(path_, Issue(c));
    std::unique_ptr<Manager> m;
    EXPECT_EQ(Open(&m), Error::Ok);
    return m;
  }

  TempDir dir_;
  std::string path_;
  std::atomic<int64_t> now_{0};
};

TEST_F(ManagerTest, OpenFailures) {
  std::unique_ptr<Manager> m;
  EXPECT_EQ(Open(&m), Error::FileNotFound);

  ReplaceFile(path_, Replace(Issue(Contract{}), "\"offline_stt\": 8", "\"offline_stt\": 80"));
  EXPECT_EQ(Open(&m), Error::BadSignature);

  ReplaceFile(path_, "not json");
  EXPECT_EQ(Open(&m), Error::ParseError);

  Contract future;
  future.not_before = "2027-01-01T00:00:00Z";
  ReplaceFile(path_, Issue(future));
  EXPECT_EQ(Open(&m), Error::NotYetValid);

  Contract old;
  old.not_before = "2025-01-01T00:00:00Z";
  old.not_after = "2025-12-31T23:59:59Z";
  ReplaceFile(path_, Issue(old));
  EXPECT_EQ(Open(&m), Error::Expired);
  EXPECT_EQ(m, nullptr);

  // 다른 키로 서명된 정상 형식 파일
  ReplaceFile(path_, Issue(Contract{}, TestSeed(50)));
  EXPECT_EQ(Open(&m), Error::BadSignature);

  ReplaceFile(path_, Issue(Contract{}));
  EXPECT_EQ(Open(&m), Error::Ok);
  ASSERT_NE(m, nullptr);
}

TEST_F(ManagerTest, AcquireUpToLimitThenRelease) {
  auto m = MustOpen();
  EXPECT_EQ(m->MaxChannels(Kind::Offline), 8);
  EXPECT_EQ(m->MaxChannels(Kind::Online), 16);
  std::vector<Channel> held(8);
  for (auto& ch : held) ASSERT_EQ(m->Acquire(Kind::Offline, &ch), Error::Ok);
  Channel extra;
  EXPECT_EQ(m->Acquire(Kind::Offline, &extra), Error::ChannelLimit);
  EXPECT_FALSE(extra.held());
  // 종류별 독립 집계
  EXPECT_EQ(m->Acquire(Kind::Online, &extra), Error::Ok);
  EXPECT_EQ(m->Snapshot().online_used, 1);
  extra.Release();
  extra.Release();  // 중복 반납 무해
  EXPECT_EQ(m->Snapshot().online_used, 0);

  held[3].Release();
  EXPECT_EQ(m->Snapshot().offline_used, 7);
  EXPECT_EQ(m->Acquire(Kind::Offline, &extra), Error::Ok);
  EXPECT_EQ(m->Acquire(Kind::Offline, &held[3]), Error::ChannelLimit);
  held.clear();  // 소멸자 반납
  extra.Release();
  EXPECT_EQ(m->Snapshot().offline_used, 0);
}

TEST_F(ManagerTest, ChannelMoveSemantics) {
  auto m = MustOpen();
  Channel a;
  ASSERT_EQ(m->Acquire(Kind::Offline, &a), Error::Ok);
  Channel b(std::move(a));
  EXPECT_FALSE(a.held());
  EXPECT_TRUE(b.held());
  EXPECT_EQ(m->Snapshot().offline_used, 1);
  Channel c;
  ASSERT_EQ(m->Acquire(Kind::Offline, &c), Error::Ok);
  EXPECT_EQ(m->Snapshot().offline_used, 2);
  c = std::move(b);  // c의 기존 점유분 반납 후 b 인수
  EXPECT_EQ(m->Snapshot().offline_used, 1);
  c = std::move(c);  // 자기 대입 무해
  EXPECT_EQ(m->Snapshot().offline_used, 1);
  // 이미 채널을 가진 out 으로 Acquire: 기존 반납 후 새로 점유 (교착 없음)
  ASSERT_EQ(m->Acquire(Kind::Offline, &c), Error::Ok);
  EXPECT_EQ(m->Snapshot().offline_used, 1);
}

TEST_F(ManagerTest, ChannelOutlivesManager) {
  Channel ch;
  {
    auto m = MustOpen();
    ASSERT_EQ(m->Acquire(Kind::Online, &ch), Error::Ok);
  }
  EXPECT_TRUE(ch.held());
  ch.Release();  // 해제된 Manager 메모리를 건드리지 않아야 함 (ASan 으로 확인)
  EXPECT_FALSE(ch.held());
}

TEST_F(ManagerTest, GraceThenExpiredAtRuntime) {
  auto m = MustOpen();
  Channel running;
  ASSERT_EQ(m->Acquire(Kind::Offline, &running), Error::Ok);

  now_ = T("2027-09-21T00:00:00Z");  // 만료 1초 후
  Channel ch;
  EXPECT_EQ(m->Acquire(Kind::Offline, &ch), Error::Ok);
  EXPECT_EQ(m->Snapshot().state, State::Grace);
  ch.Release();

  now_ = T("2027-10-05T00:00:00Z");  // 유예 14일 종료 후
  EXPECT_EQ(m->Acquire(Kind::Offline, &ch), Error::Expired);
  lic::Info info = m->Snapshot();
  EXPECT_EQ(info.state, State::Expired);
  EXPECT_EQ(info.offline_used, 1);  // 진행 중 세션은 유지
  running.Release();
  EXPECT_EQ(m->Snapshot().offline_used, 0);
}

TEST_F(ManagerTest, LazyReloadAfterInterval) {
  auto m = MustOpen();
  Contract more;
  more.offline = 20;
  more.license_id = "11111111111111111111111111111111";
  ReplaceFile(path_, Issue(more));

  now_ += 59;
  EXPECT_EQ(m->MaxChannels(Kind::Offline), 8);  // 아직 점검 주기 전
  now_ += 1;
  EXPECT_EQ(m->MaxChannels(Kind::Offline), 20);
  EXPECT_EQ(m->Snapshot().license_id, more.license_id);
  EXPECT_EQ(m->Snapshot().last_reload_error, Error::Ok);
}

TEST_F(ManagerTest, ExplicitReloadIsImmediate) {
  auto m = MustOpen();
  Contract more;
  more.offline = 3;
  ReplaceFile(path_, Issue(more));
  EXPECT_EQ(m->Reload(), Error::Ok);
  EXPECT_EQ(m->MaxChannels(Kind::Offline), 3);
}

TEST_F(ManagerTest, BadReplacementKeepsCurrentLicense) {
  auto m = MustOpen();
  const std::string good = Issue(Contract{});

  ReplaceFile(path_, Replace(good, "\"offline_stt\": 8", "\"offline_stt\": 800"));
  EXPECT_EQ(m->Reload(), Error::BadSignature);
  lic::Info info = m->Snapshot();
  EXPECT_EQ(info.offline_max, 8);
  EXPECT_EQ(info.state, State::Valid);
  EXPECT_EQ(info.last_reload_error, Error::BadSignature);
  Channel ch;
  EXPECT_EQ(m->Acquire(Kind::Offline, &ch), Error::Ok);  // 서비스 계속
  ch.Release();

  std::filesystem::remove(path_);
  now_ += 60;
  EXPECT_EQ(m->Snapshot().last_reload_error, Error::FileNotFound);
  EXPECT_EQ(m->MaxChannels(Kind::Offline), 8);

  // 만료된 라이선스로의 교체 거부
  Contract old;
  old.not_before = "2025-01-01T00:00:00Z";
  old.not_after = "2025-12-31T23:59:59Z";
  ReplaceFile(path_, Issue(old));
  EXPECT_EQ(m->Reload(), Error::Expired);
  EXPECT_EQ(m->Snapshot().state, State::Valid);

  // 원본 복구 → 오류 해제
  ReplaceFile(path_, good);
  now_ += 60;
  EXPECT_EQ(m->Snapshot().last_reload_error, Error::Ok);
}

TEST_F(ManagerTest, EarlyRenewalAppliesWhenItBecomesValid) {
  Contract cur;
  cur.not_after = "2026-12-31T23:59:59Z";
  auto m = MustOpen(cur);
  Contract renewal;
  renewal.license_id = "22222222222222222222222222222222";
  renewal.not_before = "2027-01-01T00:00:00Z";
  renewal.not_after = "2027-12-31T23:59:59Z";
  ReplaceFile(path_, Issue(renewal));  // 미개시 갱신본을 미리 배치

  now_ += 60;
  lic::Info info = m->Snapshot();
  EXPECT_EQ(info.license_id, cur.license_id);  // 아직 기존 것 사용
  EXPECT_EQ(info.last_reload_error, Error::NotYetValid);

  now_ = T("2027-01-01T00:00:30Z");  // 기존 만료 후(유예 중) + 갱신본 개시
  info = m->Snapshot();
  EXPECT_EQ(info.license_id, renewal.license_id);
  EXPECT_EQ(info.state, State::Valid);
  EXPECT_EQ(info.last_reload_error, Error::Ok);
}

TEST_F(ManagerTest, LimitReductionDoesNotEvict) {
  auto m = MustOpen();
  std::vector<Channel> held(6);
  for (auto& ch : held) ASSERT_EQ(m->Acquire(Kind::Offline, &ch), Error::Ok);
  Contract fewer;
  fewer.offline = 2;
  ReplaceFile(path_, Issue(fewer));
  ASSERT_EQ(m->Reload(), Error::Ok);
  EXPECT_EQ(m->Snapshot().offline_used, 6);  // 끊지 않음
  Channel ch;
  EXPECT_EQ(m->Acquire(Kind::Offline, &ch), Error::ChannelLimit);
  held.resize(2);  // 4개 반납 → used 2
  EXPECT_EQ(m->Acquire(Kind::Offline, &ch), Error::ChannelLimit);
  held.resize(1);
  EXPECT_EQ(m->Acquire(Kind::Offline, &ch), Error::Ok);
}

TEST_F(ManagerTest, ClockRollbackTriggersRecheck) {
  auto m = MustOpen();
  Contract more;
  more.offline = 9;
  ReplaceFile(path_, Issue(more));
  now_ -= 3600;  // 시계를 과거로
  EXPECT_EQ(m->MaxChannels(Kind::Offline), 9);
}

TEST_F(ManagerTest, InfoJson) {
  auto m = MustOpen();
  Channel ch;
  ASSERT_EQ(m->Acquire(Kind::Online, &ch), Error::Ok);
  EXPECT_EQ(lic::InfoToJson(m->Snapshot()),
            "{\"channels\":{\"offline_max\":8,\"offline_used\":0,\"online_max\":16,"
            "\"online_used\":1},\"checked_at\":\"2026-10-01T00:00:00Z\","
            "\"grace_until\":\"2027-10-04T23:59:59Z\",\"issued_at\":\"2026-09-21T00:00:00Z\","
            "\"last_reload_error\":\"OK\",\"license_id\":\"0123456789abcdef0123456789abcdef\","
            "\"license_type\":\"production\",\"not_after\":\"2027-09-20T23:59:59Z\","
            "\"not_before\":\"2026-09-21T00:00:00Z\",\"project_name\":\"STT Platform "
            "Modernization\",\"site_id\":\"SEOUL-MAIN-DC\",\"state\":\"VALID\"}");
}

TEST_F(ManagerTest, ConcurrentAcquireNeverExceedsLimit) {
  Contract c;
  c.offline = 5;
  auto m = MustOpen(c);
  std::atomic<int> inside{0}, peak{0}, granted{0}, denied{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < 16; ++t) {
    threads.emplace_back([&] {
      for (int i = 0; i < 2000; ++i) {
        Channel ch;
        if (m->Acquire(Kind::Offline, &ch) != Error::Ok) { ++denied; continue; }
        ++granted;
        int now_inside = ++inside;
        int p = peak.load();
        while (now_inside > p && !peak.compare_exchange_weak(p, now_inside)) {}
        --inside;
      }  // ch 소멸 → 반납
    });
  }
  for (auto& th : threads) th.join();
  EXPECT_LE(peak.load(), 5);
  EXPECT_GT(granted.load(), 0);
  EXPECT_EQ(granted + denied, 16 * 2000);
  EXPECT_EQ(m->Snapshot().offline_used, 0);
}

TEST_F(ManagerTest, ConcurrentReloadAndAcquire) {
  auto m = MustOpen();
  Contract a, b;
  b.offline = 4;
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
    users.emplace_back([&] {
      while (!stop) {
        Channel ch;
        m->Acquire(Kind::Offline, &ch);
        m->Snapshot();
      }
    });
  }
  writer.join();
  for (auto& u : users) u.join();
  EXPECT_EQ(m->Snapshot().offline_used, 0);
}

}  // namespace

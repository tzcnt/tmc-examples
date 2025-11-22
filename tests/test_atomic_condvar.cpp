#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/atomic_condvar.hpp"

#include <atomic>
#include <gtest/gtest.h>

#include <array>
#include <thread>

#define CATEGORY test_atomic_condvar

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static tmc::task<void>
make_waiter(tmc::atomic_condvar<int>& CV, atomic_awaitable<int>& AA) {
  co_await CV.await(1);
  AA.inc();
};

TEST_F(CATEGORY, nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::atomic_condvar<int> cv(1);
    EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
    co_await cv.await(2);
    cv.ref().store(2, std::memory_order_relaxed);
    co_await cv.await(3);
  }());
}

TEST_F(CATEGORY, one_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::atomic_condvar<int> cv(1);
    atomic_awaitable<int> aa(1);
    auto t = tmc::spawn(make_waiter(cv, aa)).fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
    EXPECT_EQ(aa.load(), 0);
    cv.ref()++;
    cv.notify_one();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, multi_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::atomic_condvar<int> cv(1);
    atomic_awaitable<int> aa(5);
    std::array<tmc::task<void>, 5> tasks;
    for (size_t i = 0; i < 5; ++i) {
      tasks[i] = make_waiter(cv, aa);
    }
    auto t = tmc::spawn_many(tasks).fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
    EXPECT_EQ(aa.load(), 0);
    cv.ref()++;
    cv.notify_one();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 1);
    cv.notify_n(2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 3);
    cv.notify_all();
    co_await aa;
    EXPECT_EQ(aa.load(), 5);
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::atomic_condvar<int>> cv;
    cv.emplace(1);
    auto t = tmc::spawn(make_waiter(*cv, aa)).fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 0);

    // Notify 0 waiters
    cv->notify_n(0);
    co_await cv->co_notify_n(0);

    // Notify waiters, but the value hasn't changed
    cv->notify_n(1);
    cv->notify_one();
    cv->notify_all();
    co_await cv->co_notify_n(1);
    co_await cv->co_notify_one();
    co_await cv->co_notify_all();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 0);

    // Destroy cv while the task is still waiting.
    cv.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, co_notify_one) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::atomic_condvar<int> cv(1);
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(make_waiter(cv, aa)).fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
      EXPECT_EQ(aa.load(), 0);
      cv.ref()++;
      co_await cv.co_notify_one();
      co_await aa;
      co_await std::move(t);
    }
    {
      tmc::atomic_condvar<int> cv(1);
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 make_waiter(cv, aa)

      )
                 .with_priority(1)
                 .fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
      EXPECT_EQ(aa.load(), 0);
      cv.ref()++;
      co_await cv.co_notify_one();
      co_await aa;
      co_await std::move(t);
    }
  }());
}

TEST_F(CATEGORY, co_notify_n) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::atomic_condvar<int> cv(1);
      atomic_awaitable<int> aa(2);
      auto t =
        tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa)).fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
      EXPECT_EQ(aa.load(), 0);
      cv.ref()++;
      co_await cv.co_notify_n(3);
      co_await aa;
      co_await std::move(t);
    }
    {
      tmc::atomic_condvar<int> cv(1);
      atomic_awaitable<int> aa(2);
      auto t = tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa))
                 .with_priority(1)
                 .fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
      EXPECT_EQ(aa.load(), 0);
      cv.ref()++;
      co_await cv.co_notify_n(3);
      co_await aa;
      co_await std::move(t);
    }
  }());
}

TEST_F(CATEGORY, co_notify_all) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::atomic_condvar<int> cv(1);
      atomic_awaitable<int> aa(2);
      auto t =
        tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa)).fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
      EXPECT_EQ(aa.load(), 0);
      cv.ref()++;
      co_await cv.co_notify_all();
      co_await aa;
      co_await std::move(t);
    }
    {
      tmc::atomic_condvar<int> cv(1);
      atomic_awaitable<int> aa(2);
      auto t = tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa))
                 .with_priority(1)
                 .fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
      EXPECT_EQ(aa.load(), 0);
      cv.ref()++;
      co_await cv.co_notify_all();
      co_await aa;
      co_await std::move(t);
    }
  }());
}

#undef CATEGORY

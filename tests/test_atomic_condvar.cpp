#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/atomic_condvar.hpp"
#include "tmc/current.hpp"
#include "waiter_count_accessor.hpp"

#include <atomic>
#include <gtest/gtest.h>

#include <array>
#include <type_traits>

#define CATEGORY test_atomic_condvar

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }

  using waiter_count_accessor = tmc::tests::waiter_count_accessor;
};

static tmc::task<void>
make_waiter(tmc::atomic_condvar<int>& CV, atomic_awaitable<int>& AA) {
  co_await CV.await(1);
  AA.inc();
};

TEST_F(CATEGORY, no_waiters) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::atomic_condvar<int> cv(1);
    cv.notify_one();
    cv.notify_n(5);
    cv.notify_all();
    EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
    co_return;
  }());
}

TEST_F(CATEGORY, no_waiters_co_notify) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::atomic_condvar<int> cv(1);
    co_await cv.co_notify_one();
    co_await cv.co_notify_n(5);
    co_await cv.co_notify_all();
    EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
    co_return;
  }());
}

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
    co_await waiter_count_accessor::wait_for_waiter_count(cv, 1);
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
    co_await waiter_count_accessor::wait_for_waiter_count(cv, 5);
    EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
    EXPECT_EQ(aa.load(), 0);
    cv.ref()++;
    cv.notify_one();
    co_await waiter_count_accessor::wait_for_waiter_count(cv, 4);
    cv.notify_n(2);
    co_await waiter_count_accessor::wait_for_waiter_count(cv, 2);
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
    co_await waiter_count_accessor::wait_for_waiter_count(*cv, 1);
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

    // The value hasn't changed, so none of the notifies above should have
    // woken the waiter. Verify it is still waiting.
    EXPECT_EQ(waiter_count_accessor::waiter_count(*cv), 1u);
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
      co_await waiter_count_accessor::wait_for_waiter_count(cv, 1);
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

      // Run at a lower priority so we can't starve the waiter while spinning.
      co_await tmc::change_priority(1);

      auto t = tmc::spawn(
                 make_waiter(cv, aa)

      )
                 .with_priority(0)
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(cv, 1);
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
      auto t = tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa)).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(cv, 2);
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

      // Run at a lower priority so we can't starve the waiter while spinning.
      co_await tmc::change_priority(1);

      auto t = tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa))
                 .with_priority(0)
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(cv, 2);
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
      auto t = tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa)).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(cv, 2);
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

      // Run at a lower priority so we can't starve the waiter while spinning.
      co_await tmc::change_priority(1);

      auto t = tmc::spawn_tuple(make_waiter(cv, aa), make_waiter(cv, aa))
                 .with_priority(0)
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(cv, 2);
      EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
      EXPECT_EQ(aa.load(), 0);
      cv.ref()++;
      co_await cv.co_notify_all();
      co_await aa;
      co_await std::move(t);
    }
  }());
}

// The task should not be symmetric transferred as it is scheduled with a
// different priority.
TEST_F(CATEGORY, co_notify_no_symmetric) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::atomic_condvar<int> cv(1);
    atomic_awaitable<int> aa(1);

    // Run at a lower priority so we can't starve the waiter while spinning.
    co_await tmc::change_priority(1);

    auto t = tmc::spawn(make_waiter(cv, aa)).with_priority(0).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(cv, 1);
    EXPECT_EQ(cv.ref().load(std::memory_order_relaxed), 1);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 1);
    cv.ref()++;
    co_await cv.co_notify_one();
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await aa;
    co_await std::move(t);
  }());
}

// The atomic_condvar awaitables are movable so that utility functions
// (spawn(), fork_group, spawn_group, ...) can capture them by value into a
// wrapper task. This makes it safe to pass a temporary and defer the
// co_await.
static_assert(std::is_move_constructible_v<tmc::aw_atomic_condvar<int>>);
static_assert(!std::is_copy_constructible_v<tmc::aw_atomic_condvar<int>>);
static_assert(
  std::is_move_constructible_v<tmc::aw_atomic_condvar_co_notify<int>>
);
static_assert(
  !std::is_copy_constructible_v<tmc::aw_atomic_condvar_co_notify<int>>
);

TEST_F(CATEGORY, fork_temporary_await) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::atomic_condvar<int> cv(1);
    // The temporary returned by await() is destroyed at the end of this
    // statement, but the forked wrapper task owns a copy of it, which
    // suspends until the value changes.
    auto t = tmc::spawn(cv.await(1)).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(cv, 1);
    cv.ref().store(2, std::memory_order_seq_cst);
    cv.notify_one();
    co_await std::move(t);
    co_await waiter_count_accessor::wait_for_waiter_count(cv, 0);
  }());
}

#undef CATEGORY

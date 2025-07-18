#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/mutex.hpp"

#include <gtest/gtest.h>

#include <array>
#include <thread>

#define CATEGORY test_mutex

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    EXPECT_EQ(mut.is_locked(), false);
    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);
    mut.unlock();
    EXPECT_EQ(mut.is_locked(), false);
    {
      tmc::mutex_scope s{co_await mut.lock_scope()};
      EXPECT_EQ(mut.is_locked(), true);
    }
    EXPECT_EQ(mut.is_locked(), false);
    {
      auto s = co_await mut.lock_scope();
      EXPECT_EQ(mut.is_locked(), true);
    }
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, one_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Mut;
          AA.inc();
        }(mut, aa)
      )
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    mut.unlock();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, multi_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    atomic_awaitable<int> aa(5);
    std::array<tmc::task<void>, 5> tasks;
    for (size_t i = 0; i < 5; ++i) {
      tasks[i] =
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Mut;
        AA.inc();
        Mut.unlock();
      }(mut, aa);
    }
    auto t = tmc::spawn_many(tasks).fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    mut.unlock();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    co_await aa;
    EXPECT_EQ(aa.load(), 5);
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::mutex> mut;
    mut.emplace();
    co_await *mut;
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Mut;
          AA.inc();
        }(*mut, aa)
      )
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 0);
    // Destroy mut while the task is still waiting.
    mut.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, move_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::mutex> mut;
    mut.emplace();
    std::optional<tmc::mutex_scope> scope{co_await mut->lock_scope()};
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Mut;
          AA.inc();
        }(*mut, aa)
      )
        .fork();
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(aa.load(), 0);
      auto s = *std::move(scope);
      scope.reset(); // should do nothing as the scope has been moved
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(aa.load(), 0);
    }
    co_await aa;
    co_await std::move(t);
  }());
}

#ifndef TSAN_ENABLED

// Protect access to a non-atomic resource with acquire/release semantics
TEST_F(CATEGORY, access_control) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::mutex mut;

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&mut, &count](int i) -> tmc::task<void> {
          return [](tmc::mutex& Mut, size_t& Count) -> tmc::task<void> {
            co_await Mut;
            ++Count;
            Mut.unlock();
          }(mut, count);
        }
      ),
      1000
    );
    co_await mut;
    EXPECT_EQ(count, 1000);
  }());
}

TEST_F(CATEGORY, access_control_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::mutex mut;
    co_await mut;

    auto ts =
      tmc::spawn_many(
        tmc::iter_adapter(
          0,
          [&mut, &count](int i) -> tmc::task<void> {
            return [](tmc::mutex& Mut, size_t& Count) -> tmc::task<void> {
              auto s = co_await Mut.lock_scope();
              ++Count;
            }(mut, count);
          }
        ),
        1000
      )
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    mut.unlock();
    co_await std::move(ts);
    co_await mut;
    EXPECT_EQ(count, 1000);
  }());
}

#endif // TSAN_ENABLED

TEST_F(CATEGORY, co_unlock) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    {
      co_await mut;
      EXPECT_EQ(mut.is_locked(), true);
      co_await mut.co_unlock();
      EXPECT_EQ(mut.is_locked(), false);
      co_await mut;
      EXPECT_EQ(mut.is_locked(), true);
    }
    {
      atomic_awaitable<int> aa(1);
      auto t =
        tmc::spawn(
          [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
            co_await Mut;
            AA.inc();
          }(mut, aa)
        )
          .fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(mut.is_locked(), true);
      EXPECT_EQ(aa.load(), 0);
      co_await mut.co_unlock();
      co_await aa;
      co_await std::move(t);
    }
  }());
}

// The task should not be symmetric transferred as it is scheduled with a
// different priority.
TEST_F(CATEGORY, co_unlock_no_symmetric) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 1);
          co_await Mut;
          EXPECT_EQ(tmc::current_priority(), 1);
          AA.inc();
        }(mut, aa)
      )
        .with_priority(1)
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await mut.co_unlock();
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await aa;
    co_await std::move(t);
  }());
}

#undef CATEGORY

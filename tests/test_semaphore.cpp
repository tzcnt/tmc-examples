#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/semaphore.hpp"
#include "tmc/utils.hpp"

#include <gtest/gtest.h>

#include <array>
#include <thread>

#define CATEGORY test_semaphore

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
    tmc::semaphore sem(1);
    EXPECT_EQ(sem.count(), 1);
    co_await sem;
    EXPECT_EQ(sem.count(), 0);
    sem.release();
    EXPECT_EQ(sem.count(), 1);
    sem.release(2);
    EXPECT_EQ(sem.count(), 3);
    co_await sem;
    EXPECT_EQ(sem.count(), 2);
    co_await sem;
    EXPECT_EQ(sem.count(), 1);
    co_await sem;
    EXPECT_EQ(sem.count(), 0);
  }());
}

TEST_F(CATEGORY, one_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);
    co_await sem;

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn(
        [](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Sem;
          AA.inc();
        }(sem, aa)
      )
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    sem.release();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, multi_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(5);
    std::array<tmc::task<void>, 5> tasks;
    for (size_t i = 0; i < 5; ++i) {
      tasks[i] =
        [](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Sem;
        AA.inc();
      }(sem, aa);
    }
    auto t = tmc::spawn_many(tasks).fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    sem.release(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 1);
    sem.release(4);
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::semaphore> sem;
    sem.emplace(0);
    auto t =
      tmc::spawn(
        [](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Sem;
          AA.inc();
        }(*sem, aa)
      )
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 0);
    // Destroy sem while the task is still waiting.
    sem.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

// Sem should be usable as a semaphore to protect access to a non-atomic
// resource with acquire/release semantics
TEST_F(CATEGORY, access_control) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::semaphore sem(1);

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&sem, &count](int i) -> tmc::task<void> {
          return [](tmc::semaphore& Sem, size_t& Count) -> tmc::task<void> {
            co_await Sem;
            ++Count;
            Sem.release();
          }(sem, count);
        }
      ),
      100
    );
    co_await sem;
    EXPECT_EQ(count, 100);
  }());
}

TEST_F(CATEGORY, co_release) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);
    {
      co_await sem;
      EXPECT_EQ(sem.count(), 0);
      co_await sem.co_release();
      EXPECT_EQ(sem.count(), 1);
      co_await sem;
      EXPECT_EQ(sem.count(), 0);
    }
    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::semaphore& Sem, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Sem;
                   AA.inc();
                   Sem.release();
                 }(sem, aa)
      )
                 .fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(sem.count(), 0);
      EXPECT_EQ(aa.load(), 0);
      co_await sem.co_release();
      co_await aa;
      co_await std::move(t);
      co_await sem;
    }
    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::semaphore& Sem, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Sem;
                   AA.inc();
                   Sem.release();
                 }(sem, aa)
      )
                 .with_priority(1)
                 .fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(sem.count(), 0);
      EXPECT_EQ(aa.load(), 0);
      co_await sem.co_release();
      co_await aa;
      co_await std::move(t);
      co_await sem;
    }
  }());
}

#undef CATEGORY

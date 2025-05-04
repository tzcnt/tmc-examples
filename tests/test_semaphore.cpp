#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/semaphore.hpp"

#include <gtest/gtest.h>

#include <array>
#include <thread>

#define CATEGORY test_semaphore

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
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

#undef CATEGORY

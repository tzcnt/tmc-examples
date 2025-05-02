// Various tests to increase code coverage in specific areas that are otherwise
// not exercised.

#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/barrier.hpp"
#include "tmc/detail/qu_inbox.hpp"
#include "tmc/external.hpp"
#include "tmc/sync.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <vector>

#define CATEGORY test_barrier

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

tmc::task<void> waiter(
  tmc::barrier& B, std::vector<std::atomic<bool>>& DoneArray, size_t DoneIdx
) {
  DoneArray[DoneIdx].store(true, std::memory_order_relaxed);
  co_await B;
  // Each waiter should see all other waiters as done after the barrier has been
  // passed.
  for (auto& d : DoneArray) {
    EXPECT_EQ(d.load(std::memory_order_relaxed), true);
  }
}

// Alternates between modification and verification phases
tmc::task<void> flip_flop_waiter(
  tmc::barrier& B, std::vector<std::atomic<bool>>& DoneArray, size_t DoneIdx
) {
  for (size_t i = 0; i < 10; ++i) {
    DoneArray[DoneIdx].store(true, std::memory_order_relaxed);
    co_await B;

    for (auto& d : DoneArray) {
      EXPECT_EQ(d.load(std::memory_order_relaxed), true);
    }
    co_await B;

    DoneArray[DoneIdx].store(false, std::memory_order_relaxed);
    co_await B;

    for (auto& d : DoneArray) {
      EXPECT_EQ(d.load(std::memory_order_relaxed), false);
    }
    co_await B;
  }
}

TEST_F(CATEGORY, once) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(5);
    std::vector<tmc::task<void>> tasks(5);
    std::vector<std::atomic<bool>> dones(5);
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);
  }());
}

TEST_F(CATEGORY, auto_reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(5);
    std::vector<tmc::task<void>> tasks(5);
    std::vector<std::atomic<bool>> dones(5);
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);

    for (size_t i = 0; i < tasks.size(); ++i) {
      dones[i].store(false, std::memory_order_relaxed);
      tasks[i] = waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);
  }());
}

TEST_F(CATEGORY, flip_flop) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(5);
    std::vector<tmc::task<void>> tasks(5);
    std::vector<std::atomic<bool>> dones(5);
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = flip_flop_waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);
  }());
}

#undef CATEGORY

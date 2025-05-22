#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/latch.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <thread>
#include <vector>

#define CATEGORY test_latch

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

tmc::task<void> waiter(
  tmc::latch& B, std::vector<std::atomic<bool>>& DoneArray, size_t DoneIdx
) {
  DoneArray[DoneIdx].store(true, std::memory_order_relaxed);
  B.count_down();
  co_await B;
  // Each waiter should see all other waiters as done after the latch has been
  // passed.
  for (auto& d : DoneArray) {
    EXPECT_EQ(d.load(std::memory_order_relaxed), true);
  }
}

TEST_F(CATEGORY, one_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::latch lat(1);
    lat.count_down();
    co_await lat;
    co_await lat;
  }());
}

TEST_F(CATEGORY, zero_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::latch lat(0);
    co_await lat;
    co_await lat;
  }());
}

TEST_F(CATEGORY, negative_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::latch lat(static_cast<size_t>(-1));
    co_await lat;
    co_await lat;
  }());
}

TEST_F(CATEGORY, once) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    tmc::latch lat(1);
    auto t =
      tmc::spawn(
        [](tmc::latch& Lat, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Lat;
          AA.inc();
        }(lat, aa)
      )
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(lat.is_ready());
    EXPECT_EQ(aa.load(), 0);
    lat.count_down();
    EXPECT_TRUE(lat.is_ready());
    co_await aa;
    EXPECT_EQ(aa.load(), 1);
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, once_parallel) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::latch lat(5);
    std::vector<tmc::task<void>> tasks(5);
    std::vector<std::atomic<bool>> dones(5);
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = waiter(lat, dones, i);
    }
    co_await tmc::spawn_many(tasks);
    EXPECT_TRUE(lat.is_ready());
    co_await lat;
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::latch> lat;
    lat.emplace(100);
    auto t =
      tmc::spawn(
        [](tmc::latch& Lat, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Lat;
          AA.inc();
        }(*lat, aa)
      )
        .fork();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 0);
    // Destroy lat while the task is still waiting.
    lat.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

#undef CATEGORY

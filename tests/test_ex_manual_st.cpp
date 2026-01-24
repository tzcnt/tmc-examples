#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/detail/compat.hpp"

#include <gtest/gtest.h>

#include <thread>

#define CATEGORY test_ex_manual_st

class CATEGORY : public testing::Test {
  static inline tmc::ex_manual_st exec;
  static inline std::jthread worker;
  static inline std::stop_source stopper;

protected:
  // Setup a worker thread to pump the executor queue.
  // This allows us to run the same test suite as all the other executors.
  static void SetUpTestSuite() {
    exec.set_priority_count(2).init();
    worker = std::jthread([&](std::stop_token ThreadStopToken) -> void {
      size_t i = 0;
      while (!ThreadStopToken.stop_requested()) {
        // Test all 3 run() methods
        switch (i % 3) {
        case 0:
          exec.run_all();
          break;
        case 1:
          exec.run_n(TMC_ALL_ONES);
          break;
        case 2:
          while (exec.run_one()) {
          }
          break;
        }
        ++i;
        std::this_thread::yield();
      }
    });
    stopper = worker.get_stop_source();
  }

  static void TearDownTestSuite() {
    stopper.request_stop();
    worker.join();
    exec.teardown();
  }

  static tmc::ex_manual_st& ex() { return exec; }
};

// If an invalid priority is submitted, ex_manual_st will clamp it to the valid
// range.
TEST_F(CATEGORY, clamp_priority) {
  auto fut = tmc::post_waitable(
    ex(),
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), ex().priority_count() - 1);
      co_return;
    }(),
    2
  );
  fut.wait();
}

TEST_F(CATEGORY, empty) {
  tmc::ex_manual_st ex;
  ex.init();
  EXPECT_TRUE(ex.empty());

  auto fut = tmc::post_waitable(ex, []() -> void {});
  EXPECT_FALSE(ex.empty());

  ex.run_one();
  EXPECT_TRUE(ex.empty());

  fut.wait();
  ex.teardown();
}

TEST_F(CATEGORY, empty_internal) {
  tmc::ex_manual_st ex;
  ex.init();
  EXPECT_TRUE(ex.empty());

  auto fut = tmc::post_waitable(ex, []() -> tmc::task<void> {
    // Use change_priority to force the task into a different internal execution
    // queue
    co_await tmc::change_priority(1);
    co_return;
  }());
  EXPECT_FALSE(ex.empty());

  // Pump the executor twice (first time requeues the task)
  ex.run_one();
  EXPECT_FALSE(ex.empty());
  ex.run_one();
  EXPECT_TRUE(ex.empty());

  fut.wait();
  ex.teardown();
}

TEST_F(CATEGORY, set_prio) {
  tmc::ex_manual_st ex;
  ex.set_priority_count(1).init();
}

TEST_F(CATEGORY, no_init) { tmc::ex_manual_st ex; }

TEST_F(CATEGORY, init_twice) {
  tmc::ex_manual_st ex;
  ex.init();
  ex.init();
}

TEST_F(CATEGORY, teardown_twice) {
  tmc::ex_manual_st ex;
  ex.teardown();
  ex.teardown();
}

TEST_F(CATEGORY, teardown_and_destroy) {
  tmc::ex_manual_st ex;
  ex.teardown();
}

#include "test_executors.ipp"
#include "test_nested_executors.ipp"
#include "test_spawn_clang.ipp"
#include "test_spawn_composition.ipp"
#include "test_spawn_func_many.ipp"
#include "test_spawn_func_many_detach.ipp"
#include "test_spawn_func_many_each.ipp"
#include "test_spawn_func_many_fork.ipp"
#include "test_spawn_many.ipp"
#include "test_spawn_many_detach.ipp"
#include "test_spawn_many_each.ipp"
#include "test_spawn_many_fork.ipp"
#include "test_spawn_tuple.ipp"

#undef CATEGORY

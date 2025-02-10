#include "test_common.hpp"

#include <gtest/gtest.h>

#define CATEGORY assert_spawn_DeathTest

TEST(CATEGORY, spawn_run_early) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto x =
          tmc::spawn([]() -> tmc::task<void> { co_return; }()).run_early();
        co_return;
      }());
    },
    "done_count"
  );
}
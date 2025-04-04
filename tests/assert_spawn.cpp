#include "test_common.hpp"

#include <gtest/gtest.h>

#define CATEGORY assert_spawn_DeathTest

#ifndef NDEBUG
TEST(CATEGORY, spawn_fork) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto x = tmc::spawn([]() -> tmc::task<void> { co_return; }()).fork();
        co_return;
      }());
    },
    "done_count"
  );
}
#endif

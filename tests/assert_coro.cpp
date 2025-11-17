#include "test_common.hpp"

#include <gtest/gtest.h>

#ifndef NDEBUG
#ifndef TMC_TRIVIAL_TASK
#define CATEGORY assert_coro_DeathTest
TEST(CATEGORY, none) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto x = []() -> tmc::task<void> { co_return; }();
        co_return;
      }());
    },
    "co_await"
  );
}

TEST(CATEGORY, co_await_twice) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto x = []() -> tmc::task<void> { co_return; }();
        co_await std::move(x);
        co_await std::move(x);
      }());
    },
    "once"
  );
}
#endif
#undef CATEGORY
#endif

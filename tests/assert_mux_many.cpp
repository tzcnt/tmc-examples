#include "test_common.hpp"

#include <gtest/gtest.h>

#include <array>

#ifndef NDEBUG
#define CATEGORY assert_mux_many_DeathTest

// An eagerly-constructed mux_many has in-flight tasks; destroying it without
// awaiting all results trips the destructor assert.
TEST(CATEGORY, not_awaited) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        std::array<tmc::task<void>, 1> arr{[]() -> tmc::task<void> { co_return; }()};
        [[maybe_unused]] auto mux = tmc::mux_many<1>(arr.begin());
        co_return;
      }());
    },
    "co_await"
  );
}

// fork() may only be called on a slot whose previous result has already been
// consumed by co_await. Forking a still-pending slot trips the assert.
TEST(CATEGORY, fork_pending_slot) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto mux = tmc::mux_many<int, 1>();
        mux.fork(0, []() -> tmc::task<int> { co_return 1; }());
        // Slot 0 is still pending (its result was never awaited).
        mux.fork(0, []() -> tmc::task<int> { co_return 2; }());
        co_return;
      }());
    },
    "fork"
  );
}

// fork() with an index outside the group's capacity trips the range assert.
TEST(CATEGORY, fork_out_of_range) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto mux = tmc::mux_many<int, 1>();
        mux.fork(5, []() -> tmc::task<int> { co_return 1; }());
        co_return;
      }());
    },
    "range"
  );
}

#undef CATEGORY
#endif

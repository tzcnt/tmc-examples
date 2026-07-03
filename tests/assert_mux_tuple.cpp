#include "test_common.hpp"

#include <gtest/gtest.h>

#ifndef NDEBUG
#define CATEGORY assert_mux_tuple_DeathTest

// An eagerly-constructed mux_tuple has in-flight tasks; destroying it without
// awaiting all results trips the destructor assert.
TEST(CATEGORY, not_awaited) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        [[maybe_unused]] tmc::mux_tuple mux([]() -> tmc::task<void> { co_return; }());
        co_return;
      }());
    },
    "You must wait"
  );
}

// fork<I>() may only be called on a slot whose previous result has already
// been consumed by co_await. Forking a still-pending slot trips the assert.
TEST(CATEGORY, fork_pending_slot) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        tmc::mux_tuple<int> mux;
        mux.fork<0>([]() -> tmc::task<int> { co_return 1; }());
        // Slot 0 is still pending (its result was never awaited).
        mux.fork<0>([]() -> tmc::task<int> { co_return 2; }());
        co_return;
      }());
    },
    "fork"
  );
}

#undef CATEGORY
#endif

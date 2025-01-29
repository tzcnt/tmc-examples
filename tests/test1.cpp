#include "tmc/all_headers.hpp"

#include <gtest/gtest.h>

TEST(Primary, TMC) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  tmc::post_waitable(ex, []() -> tmc::task<void> { co_return; }(), 0).wait();
}

tmc::task<void> empty_task() { co_return; }

#ifndef NDEBUG
TEST(Asserts, TMC) {
  // Accidentally submitted a task-returning func without calling it
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      tmc::post_waitable(ex, empty_task, 0).wait();
    },
    "!handle"
  );

  // spawn() from a non-TMC thread without default executor configured
  EXPECT_DEATH({ tmc::spawn(empty_task()).detach(); }, "executor != nullptr");
  EXPECT_DEATH(
    {
      tmc::task<void> tasks[2];
      tasks[0] = empty_task();
      tasks[1] = empty_task();
      tmc::spawn_many<2>(&tasks[0]).detach();
    },
    "executor != nullptr"
  );
}
#endif
#include "test_common.hpp"

#include <gtest/gtest.h>

#define CATEGORY assert_ex_cpu_DeathTest

#ifndef NDEBUG
TEST(CATEGORY, too_many_threads) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_thread_count(65).init();
    },
    "thread_count"
  );
}

TEST(CATEGORY, invalid_priority) {
  // Accidentally submitted a task-returning func without calling it
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      tmc::post_waitable(ex, empty_task, 1).wait();
    },
    "Priority"
  );
}

TEST(CATEGORY, task_func_post) {
  // Accidentally submitted a task-returning func without calling it
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      tmc::post_waitable(ex, empty_task, 0).wait();
    },
    "!handle"
  );
}

TEST(CATEGORY, spawn_without_executor) {
  EXPECT_DEATH({ tmc::spawn(empty_task()).detach(); }, "executor != nullptr");
}

TEST(CATEGORY, spawn_many_without_executor) {
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

TEST(CATEGORY, spawn_tuple_without_executor) {
  EXPECT_DEATH(
    { tmc::spawn_tuple(empty_task(), empty_task()).detach(); },
    "executor != nullptr"
  );
}

#endif
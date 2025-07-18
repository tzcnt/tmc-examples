#include "test_common.hpp"

#include <gtest/gtest.h>

// This file does bad things on purpose, so disable the compiler warnings for
// them.
#pragma warning(push, 0)
#pragma clang diagnostic push
#pragma GCC diagnostic push

#pragma clang diagnostic ignored "-Wunused-result"
#pragma GCC diagnostic ignored "-Wunused-result"

#define CATEGORY assert_ex_cpu_DeathTest

#ifndef NDEBUG
TEST(CATEGORY, too_many_threads) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(65).init();
    },
    "ThreadCount"
  );
}

TEST(CATEGORY, spawn_without_executor) {
  EXPECT_DEATH({ tmc::spawn(empty_task()).detach(); }, "executor != nullptr");
  EXPECT_DEATH(
    { auto x = tmc::spawn(empty_task()).fork(); }, "executor != nullptr"
  );
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
  EXPECT_DEATH(
    {
      tmc::task<void> tasks[2];
      tasks[0] = empty_task();
      tasks[1] = empty_task();
      tmc::spawn_many<2>(&tasks[0]).fork();
    },
    "executor != nullptr"
  );
}

TEST(CATEGORY, spawn_tuple_without_executor) {
  EXPECT_DEATH(
    { tmc::spawn_tuple(empty_task(), empty_task()).detach(); },
    "executor != nullptr"
  );
  EXPECT_DEATH(
    { tmc::spawn_tuple(empty_task(), empty_task()).fork(); },
    "executor != nullptr"
  );
}

#endif

#pragma GCC diagnostic pop
#pragma clang diagnostic pop
#pragma warning(pop)

#include "test_common.hpp"

#include <gtest/gtest.h>

#include <array>

#define CATEGORY assert_spawn_many_DeathTest

#ifndef NDEBUG

static std::array<tmc::task<void>, 1> task_array() {
  return std::array<tmc::task<void>, 1>{[]() -> tmc::task<void> {
    co_return;
  }()};
}

TEST(CATEGORY, none) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto arr = task_array();
        auto x = tmc::spawn_many(arr);
        co_return;
      }());
    },
    "co_await"
  );
}

TEST(CATEGORY, fork_none) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto arr = task_array();
        auto x = tmc::spawn_many(arr).fork();
        co_return;
      }());
    },
    "co_await"
  );
}

TEST(CATEGORY, each_none) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto arr = task_array();
        auto x = tmc::spawn_many(arr).result_each();
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
        auto arr = task_array();
        auto x = tmc::spawn_many(arr);
        co_await std::move(x);
        co_await std::move(x);
        co_return;
      }());
    },
    "once"
  );
}

TEST(CATEGORY, fork_co_await_twice) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto arr = task_array();
        auto x = tmc::spawn_many(arr).fork();
        co_await std::move(x);
        co_await std::move(x);
        co_return;
      }());
    },
    "once"
  );
}

TEST(CATEGORY, fork_twice) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto arr = task_array();
        auto x = tmc::spawn_many(arr);
        auto y = std::move(x).fork();
        auto z = std::move(x).fork();
        co_return;
      }());
    },
    "once"
  );
}

TEST(CATEGORY, detach_twice) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto arr = task_array();
        auto x = tmc::spawn_many(arr);
        x.detach();
        x.detach();
        co_return;
      }());
    },
    "once"
  );
}

TEST(CATEGORY, each_twice) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto arr = task_array();
        auto x = tmc::spawn_many(arr);
        auto y = std::move(x).result_each();
        auto z = std::move(x).result_each();
        co_return;
      }());
    },
    "once"
  );
}

#endif

#undef CATEGORY

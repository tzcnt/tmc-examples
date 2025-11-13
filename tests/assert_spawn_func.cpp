#include "test_common.hpp"

#include <gtest/gtest.h>

#ifndef NDEBUG
#define CATEGORY assert_spawn_func_DeathTest

TEST(CATEGORY, none) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto x = tmc::spawn_func([]() -> void {});
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
        auto x = tmc::spawn_func([]() -> void {}).fork();
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
        auto x = tmc::spawn_func([]() -> void {});
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
        auto x = tmc::spawn_func([]() -> void {}).fork();
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
        auto x = tmc::spawn_func([]() -> void {});
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
        auto x = tmc::spawn_func([]() -> void {});
        std::move(x).detach();
        std::move(x).detach();
        co_return;
      }());
    },
    "once"
  );
}

#undef CATEGORY
#endif

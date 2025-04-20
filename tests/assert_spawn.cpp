#include "test_common.hpp"

#include <gtest/gtest.h>

#define CATEGORY assert_spawn_DeathTest

#ifndef NDEBUG

TEST(CATEGORY, none) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).set_priority_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        auto x = tmc::spawn([]() -> tmc::task<void> { co_return; }());
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
        auto x = tmc::spawn([]() -> tmc::task<void> { co_return; }()).fork();
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
        auto x = tmc::spawn([]() -> tmc::task<void> { co_return; }());
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
        auto x = tmc::spawn([]() -> tmc::task<void> { co_return; }()).fork();
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
        auto x = tmc::spawn([]() -> tmc::task<void> { co_return; }());
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
        auto x = tmc::spawn([]() -> tmc::task<void> { co_return; }());
        std::move(x).detach();
        std::move(x).detach();
        co_return;
      }());
    },
    "once"
  );
}

#endif

#undef CATEGORY

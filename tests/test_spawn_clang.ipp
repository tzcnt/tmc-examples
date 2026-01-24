#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/spawn.hpp"
#include "tmc/task.hpp"

#include <gtest/gtest.h>

static tmc::task<int> spawn_clang_task_int(int value) { co_return value; }

static tmc::task<void> spawn_clang_task_void() { co_return; }

static tmc::task<void>
spawn_clang_task_check(tmc::ex_any* ExpectedExec, size_t ExpectedPrio) {
  EXPECT_EQ(tmc::current_executor(), ExpectedExec);
  EXPECT_EQ(tmc::current_priority(), ExpectedPrio);
  co_return;
}

TEST_F(CATEGORY, spawn_clang_int) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto result = co_await tmc::spawn_clang(spawn_clang_task_int(42));
    EXPECT_EQ(result, 42);
  }());
}

TEST_F(CATEGORY, spawn_clang_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await tmc::spawn_clang(spawn_clang_task_void());
  }());
}

TEST_F(CATEGORY, spawn_clang_default) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await tmc::spawn_clang(
      spawn_clang_task_check(tmc::current_executor(), 0)
    );
  }());
}

TEST_F(CATEGORY, spawn_clang_custom_executor) {
  tmc::ex_cpu_st otherExec;
  otherExec.init();
  test_async_main(ex(), [](tmc::ex_cpu_st& OtherExec) -> tmc::task<void> {
    co_await tmc::spawn_clang(
      spawn_clang_task_check(OtherExec.type_erased(), 0), OtherExec
    );
    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
  }(otherExec));
}

TEST_F(CATEGORY, spawn_clang_custom_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await tmc::spawn_clang(
      spawn_clang_task_check(tmc::current_executor(), 1),
      tmc::current_executor(), 1
    );
  }());
}

TEST_F(CATEGORY, spawn_clang_custom_both) {
  tmc::ex_cpu_st otherExec;
  otherExec.set_priority_count(2).init();
  test_async_main(ex(), [](tmc::ex_cpu_st& OtherExec) -> tmc::task<void> {
    co_await tmc::spawn_clang(
      spawn_clang_task_check(OtherExec.type_erased(), 1), OtherExec, 1
    );
    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
  }(otherExec));
}

TEST_F(CATEGORY, spawn_clang_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto result =
      co_await tmc::spawn_clang(tmc::spawn(spawn_clang_task_int(42)));
    EXPECT_EQ(result, 42);
  }());
}

TEST_F(CATEGORY, spawn_clang_type_erased_executor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::ex_any* exec = tmc::current_executor();
      auto result = co_await tmc::spawn_clang(spawn_clang_task_int(123), exec);
      EXPECT_EQ(result, 123);
    }

    {
      // also test with rvalue ex_any* parameter
      auto result = co_await tmc::spawn_clang(
        spawn_clang_task_int(2), tmc::current_executor()
      );
      EXPECT_EQ(result, 2);
    }
  }());
}

TEST_F(CATEGORY, spawn_clang_sequential) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto result1 = co_await tmc::spawn_clang(spawn_clang_task_int(1));
    auto result2 = co_await tmc::spawn_clang(spawn_clang_task_int(2));
    auto result3 = co_await tmc::spawn_clang(spawn_clang_task_int(3));
    EXPECT_EQ(result1, 1);
    EXPECT_EQ(result2, 2);
    EXPECT_EQ(result3, 3);
  }());
}

TEST_F(CATEGORY, spawn_clang_nested) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto outer_result = co_await tmc::spawn_clang([]() -> tmc::task<int> {
      auto inner_result = co_await tmc::spawn_clang(spawn_clang_task_int(100));
      co_return inner_result + 1;
    }());
    EXPECT_EQ(outer_result, 101);
  }());
}

// Tests for fork_clang and fork_tuple_clang

#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/spawn.hpp"
#include "tmc/spawn_tuple.hpp"
#include "tmc/task.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_fork_clang

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static tmc::task<int> task_int(int value) { co_return value; }

static tmc::task<void> task_void() { co_return; }

// Test fork_clang with single task returning int
TEST_F(CATEGORY, fork_clang_int) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_clang(task_int(42));
    auto result = co_await std::move(forked);
    EXPECT_EQ(result, 42);
  }());
}

// Test fork_clang with single task returning void
TEST_F(CATEGORY, fork_clang_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_clang(task_void());
    co_await std::move(forked);
  }());
}

// Test fork_clang with wrapper type awaitable returning int
// This probably doesn't allow the coro to be elided, but it should compile.
TEST_F(CATEGORY, fork_clang_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_clang(tmc::spawn(task_int(42)));
    auto result = co_await std::move(forked);
    EXPECT_EQ(result, 42);
  }());
}

// Test fork_clang with custom executor
TEST_F(CATEGORY, fork_clang_custom_executor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_clang(task_int(200), tmc::cpu_executor());
    auto result = co_await std::move(forked);
    EXPECT_EQ(result, 200);
  }());
}

// Test fork_clang with custom priority
TEST_F(CATEGORY, fork_clang_custom_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked =
      co_await tmc::fork_clang(task_int(300), tmc::current_executor(), 1);
    auto result = co_await std::move(forked);
    EXPECT_EQ(result, 300);
  }());
}

// Test fork_tuple_clang with no tasks
TEST_F(CATEGORY, fork_tuple_clang_empty) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_tuple_clang();
    [[maybe_unused]] auto results = co_await std::move(forked);
  }());
}

// Test fork_tuple_clang with multiple tasks returning int
TEST_F(CATEGORY, fork_tuple_clang_int) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked =
      co_await tmc::fork_tuple_clang(task_int(1), task_int(2), task_int(3));
    auto results = co_await std::move(forked);
    EXPECT_EQ(std::get<0>(results), 1);
    EXPECT_EQ(std::get<1>(results), 2);
    EXPECT_EQ(std::get<2>(results), 3);
  }());
}

// Test fork_tuple_clang with mixed return types
TEST_F(CATEGORY, fork_tuple_clang_mixed) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_tuple_clang(task_int(10), task_void());
    auto results = co_await std::move(forked);
    EXPECT_EQ(std::get<0>(results), 10);
    // std::get<1>(results) is std::monostate for void
  }());
}

// Test fork_tuple_clang with all void tasks
TEST_F(CATEGORY, fork_tuple_clang_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked =
      co_await tmc::fork_tuple_clang(task_void(), task_void(), task_void());
    co_await std::move(forked);
  }());
}

// Test fork_tuple_clang with single task
TEST_F(CATEGORY, fork_tuple_clang_single) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_tuple_clang(task_int(99));
    auto results = co_await std::move(forked);
    EXPECT_EQ(std::get<0>(results), 99);
  }());
}

// Test fork_clang with tmc::spawn() wrapper
TEST_F(CATEGORY, fork_clang_with_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_clang(tmc::spawn(task_int(50)));
    auto result = co_await std::move(forked);
    EXPECT_EQ(result, 50);
  }());
}

// Test fork_tuple_clang with tmc::spawn() wrappers
TEST_F(CATEGORY, fork_tuple_clang_with_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto forked = co_await tmc::fork_tuple_clang(
      tmc::spawn(task_int(5)), tmc::spawn(task_int(6))
    );
    auto results = co_await std::move(forked);
    EXPECT_EQ(std::get<0>(results), 5);
    EXPECT_EQ(std::get<1>(results), 6);
  }());
}

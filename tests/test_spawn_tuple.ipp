#include "test_common.hpp"
#include "test_spawn_many_common.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <ranges>
#include <vector>

// tests ported from examples/spawn_iterator.cpp

static inline tmc::task<void> spawn_tuple_task_func() {

  std::tuple<int, int, int> results =
    co_await tmc::spawn_tuple(work(0), work(1), work(2));

  [[maybe_unused]] auto sum =
    std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

  EXPECT_EQ(sum, (1 << 3) - 1);
}

static inline tmc::task<void> spawn_tuple_task_lambda() {
  {
    // non-capturing lambda coroutine
    auto f = [](int i) -> tmc::task<int> { co_return 1 << i; };
    std::tuple<int, int, int> results =
      co_await tmc::spawn_tuple(f(0), f(1), f(2));

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }
  {
    // capturing lambda that forwards to non-capturing lambda coroutine
    int i = 0;
    auto f = [&i]() -> tmc::task<int> {
      return [](int j) -> tmc::task<int> { co_return 1 << j; }(i++);
    };
    std::tuple<int, int, int> results =
      co_await tmc::spawn_tuple(f(), f(), f());

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }
}

static inline tmc::task<void> spawn_tuple_task_tuple() {
  std::tuple<tmc::task<int>, tmc::task<int>, tmc::task<int>> tasks{
    work(0), work(1), work(2)
  };

  std::tuple<int, int, int> results =
    co_await tmc::spawn_tuple(std::move(tasks));

  [[maybe_unused]] auto sum =
    std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

  EXPECT_EQ(sum, (1 << 3) - 1);
}

static inline tmc::task<void> spawn_tuple_task_run_early() {
  auto ts = tmc::spawn_tuple(work(0), work(1), work(2));
  auto early = std::move(ts).run_early();

  std::tuple<int, int, int> results = co_await std::move(early);

  [[maybe_unused]] auto sum =
    std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

  EXPECT_EQ(sum, (1 << 3) - 1);
}

static inline tmc::task<void> spawn_tuple_task_each() {
  auto ts = tmc::spawn_tuple(work(0), work(1), work(2));
  auto each = std::move(ts).each();

  int sum = 0;
  for (size_t i = co_await each; i != each.end(); i = co_await each) {
    switch (i) {
    case 0:
      sum += each.get<0>();
      break;
    case 1:
      sum += each.get<1>();
      break;
    case 2:
      sum += each.get<2>();
      break;
    default:
      ADD_FAILURE() << "invalid index: " << i;
      break;
    }
  }

  EXPECT_EQ(sum, (1 << 3) - 1);
}

TEST_F(CATEGORY, spawn_tuple_task_func) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_task_func();
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_lambda) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_task_func();
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_tuple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_task_tuple();
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_run_early) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_task_run_early();
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_each) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_task_each();
  }());
}

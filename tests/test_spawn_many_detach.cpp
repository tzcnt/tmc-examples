#include "atomic_awaitable.hpp"
#include "test_common.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <ranges>
#include <vector>

// tests ported from examples/spawn_iterator.cpp

static constexpr int Count = 5;

static tmc::task<int> work(int i) { co_return 1 << i; }
static bool unpredictable_filter(int i) { return i != 3; }

// This iterator produces exactly N tasks.
template <int N> auto iter_of_static_size() {
  return std::ranges::views::iota(0, N) | std::ranges::views::transform(work);
}

// This iterator produces a dynamic number of tasks,
// which can be calculated by the caller in O(1) time by
// `return.end() - return.begin()`
template <int N> auto iter_of_dynamic_known_size() {
  auto iter = std::ranges::views::iota(0, N) |
              std::ranges::views::filter(unpredictable_filter) |
              std::ranges::views::transform(work);
  return std::vector(iter.begin(), iter.end());
}

// This iterator produces a dynamic number of tasks,
// and does not support O(1) size calculation;
// `return.end() - return.begin()` will not compile.
template <int N> auto iter_of_dynamic_unknown_size() {
  return std::ranges::views::iota(0, N) |
         std::ranges::views::filter(unpredictable_filter) |
         std::ranges::views::transform(work);
}

// Test detach() when the maxCount is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_less() {
  atomic_awaitable<int> counter(0, 5);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many(tasks.begin(), tasks.end(), 5).detach();

  co_await counter;

  co_return;
}

// Test detach() when the maxCount is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_greater() {
  atomic_awaitable<int> counter(0, 10);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many(tasks.begin(), tasks.end(), 15).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 10);

  co_return;
}

// Test detach() when the Count is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_less() {
  atomic_awaitable<int> counter(0, 5);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many<5>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 5);

  co_return;
}

// Test detach() when the Count is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_greater() {
  atomic_awaitable<int> counter(0, 10);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many<15>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 10);

  co_return;
}

// Test detach() when the maxCount is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_less_uncountable_iter() {
  atomic_awaitable<int> counter(0, 5);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::filter(unpredictable_filter) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many(tasks.begin(), tasks.end(), 5).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 5);

  co_return;
}

// Test detach() when the maxCount is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_greater_uncountable_iter() {
  atomic_awaitable<int> counter(0, 9);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::filter(unpredictable_filter) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many(tasks.begin(), tasks.end(), 15).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 9);

  co_return;
}

// Test detach() when the Count is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_less_uncountable_iter() {
  atomic_awaitable<int> counter(0, 5);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::filter(unpredictable_filter) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many<5>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 5);

  co_return;
}

// Test detach() when the Count is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_greater_uncountable_iter() {
  atomic_awaitable<int> counter(0, 9);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::filter(unpredictable_filter) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many<15>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 9);

  co_return;
}

#define CATEGORY test_spawn_many_detach

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(60).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }
};

TEST_F(CATEGORY, spawn_many_all) {
  test_async_main(tmc::cpu_executor(), []() -> tmc::task<void> {
    co_await detach_maxCount_less();
    co_await detach_maxCount_greater();
    co_await detach_maxCount_template_less();
    co_await detach_maxCount_template_greater();

    co_await detach_maxCount_less_uncountable_iter();
    co_await detach_maxCount_greater_uncountable_iter();
    co_await detach_maxCount_template_less_uncountable_iter();
    co_await detach_maxCount_template_greater_uncountable_iter();
  }());
}

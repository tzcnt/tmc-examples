#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "test_spawn_func_many_common.hpp"

#include <gtest/gtest.h>

#include <ranges>

// tests ported from examples/spawn_iterator.cpp

// Test detach() when the maxCount is less than the number of tasks in the
// iterator.
static inline tmc::task<void> spawn_func_many_detach_maxCount_less() {
  atomic_awaitable<int> counter(0, 5);

  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many(tasks.begin(), tasks.end(), 5).detach();

  co_await counter;
}

// Test detach() when the maxCount is greater than the number of tasks in the
// iterator.
static inline tmc::task<void> spawn_func_many_detach_maxCount_greater() {
  atomic_awaitable<int> counter(0, 10);

  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many(tasks.begin(), tasks.end(), 15).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 10);
}

// Test detach() when the Count is less than the number of tasks in the
// iterator.
static inline tmc::task<void> spawn_func_many_detach_maxCount_template_less() {
  atomic_awaitable<int> counter(0, 5);

  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many<5>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 5);
}

// Test detach() when the Count is greater than the number of tasks in the
// iterator.
static inline tmc::task<void>
spawn_func_many_detach_maxCount_template_greater() {
  atomic_awaitable<int> counter(0, 10);

  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many<15>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 10);
}

// Test detach() when the maxCount is less than the number of tasks in the
// iterator.
static inline tmc::task<void>
spawn_func_many_detach_maxCount_less_uncountable_iter() {
  atomic_awaitable<int> counter(0, 5);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::filter(unpredictable_filter) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many(tasks.begin(), tasks.end(), 5).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 5);
}

// Test detach() when the maxCount is greater than the number of tasks in the
// iterator.
static inline tmc::task<void>
spawn_func_many_detach_maxCount_greater_uncountable_iter() {
  atomic_awaitable<int> counter(0, 9);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::filter(unpredictable_filter) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many(tasks.begin(), tasks.end(), 15).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 9);
}

// Test detach() when the Count is less than the number of tasks in the
// iterator.
static inline tmc::task<void>
spawn_func_many_detach_maxCount_template_less_uncountable_iter() {
  atomic_awaitable<int> counter(0, 5);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::filter(unpredictable_filter) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many<5>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 5);
}

// Test detach() when the Count is greater than the number of tasks in the
// iterator.
static inline tmc::task<void>
spawn_func_many_detach_maxCount_template_greater_uncountable_iter() {
  atomic_awaitable<int> counter(0, 9);

  // Iterator contains 9 tasks but end() - begin() doesn't compile
  auto tasks = std::ranges::views::iota(0, 10) |
               std::ranges::views::filter(unpredictable_filter) |
               std::ranges::views::transform([&counter](int) -> auto {
                 return [&counter]() -> void {
                   ++counter.ref();
                   counter.ref().notify_all();
                 };
               });

  tmc::spawn_func_many<15>(tasks.begin(), tasks.end()).detach();

  co_await counter;
  EXPECT_EQ(counter.load(), 9);
}

TEST_F(CATEGORY, spawn_func_many_detach_maxCount_less) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_less();
  }());
}

TEST_F(CATEGORY, spawn_func_many_detach_maxCount_greater) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_greater();
  }());
}

TEST_F(CATEGORY, spawn_func_many_detach_maxCount_template_less) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_template_less();
  }());
}

TEST_F(CATEGORY, spawn_func_many_detach_maxCount_template_greater) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_template_greater();
  }());
}

TEST_F(CATEGORY, spawn_func_many_detach_maxCount_less_uncountable_iter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_less_uncountable_iter();
  }());
}

TEST_F(CATEGORY, spawn_func_many_detach_maxCount_greater_uncountable_iter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_greater_uncountable_iter();
  }());
}

TEST_F(
  CATEGORY, spawn_func_many_detach_maxCount_template_less_uncountable_iter
) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_template_less_uncountable_iter();
  }());
}

TEST_F(
  CATEGORY, spawn_func_many_detach_maxCount_template_greater_uncountable_iter
) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_func_many_detach_maxCount_template_greater_uncountable_iter(
    );
  }());
}

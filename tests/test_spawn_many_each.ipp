#include "test_common.hpp"
#include "test_spawn_many_common.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <ranges>
#include <vector>

// tests ported from examples/spawn_iterator.cpp

template <int N> tmc::task<void> spawn_many_each_static_sized_iterator() {
  auto iter = iter_of_static_size<N>();
  // We know that the iterator produces exactly N tasks.
  // Provide the template parameter N to spawn_many, so that tasks and results
  // can be statically allocated in std::array.
  std::array<int, N> results;
  auto ts = tmc::spawn_many<N>(iter.begin()).result_each();
  for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
    results[idx] = ts[idx];
  }

  // This will produce equivalent behavior, but is not as explicit in the
  // intent auto results = co_await tmc::spawn_many<N>(iter.begin(),
  // iter.end());

  [[maybe_unused]] auto sum =
    std::accumulate(results.begin(), results.end(), 0);

  EXPECT_EQ(sum, (1 << N) - 1);

  co_return;
}

template <int N> tmc::task<void> spawn_many_each_static_bounded_iterator() {
  // In this example, we do not know the exact number of tasks that iter could
  // produce. The template parameter N serves as an upper bound on the number
  // of tasks that will be spawned. We also need to manually count the number
  // of tasks spawned. There are 2 sub-examples.
  {
    // Sub-Example 1: Iterator produces less than N tasks.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });
    std::array<int, N> results;
    auto ts = tmc::spawn_many<N>(iter.begin(), iter.end()).result_each();
    for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
      results[idx] = ts[idx];
    }

    // At this point, taskCount == 4 and N == 5.
    // The last element of results will be left default-initialized.
    EXPECT_EQ(taskCount, N - 1);

    // This extra work yields a performance benefit, because we can still use
    // std::array with an unknown-sized iterator that spawns "up to N" tasks.
    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    EXPECT_EQ(sum, (1 << N) - 1 - 8);
  }
  {
    // Sub-Example 2: Iterator could produce more than N tasks.
    // Only the first N will be taken.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N + 20>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });

    std::array<int, N> results;
    auto ts = tmc::spawn_many<N>(iter.begin(), iter.end()).result_each();
    for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
      results[idx] = ts[idx];
    }

    // At this point, taskCount == 5 and N == 5.
    // We stopped consuming elements from the iterator after N tasks.
    EXPECT_EQ(taskCount, N);
    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    EXPECT_EQ(sum, (1 << N) - 1 - 8 + (1 << N));
  }
  co_return;
}

template <int N>
tmc::task<void> spawn_many_each_dynamic_known_sized_iterator() {
  auto iter = iter_of_dynamic_known_size<N>();

  // The template parameter N to spawn_many is not provided.
  // This overload will produce a right-sized output vector
  // (internally calculated from tasks.end() - tasks.begin())
  std::vector<int> results;
  auto ts = tmc::spawn_many(iter.begin(), iter.end()).result_each();
  for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
    results.push_back(ts[idx]);
  }

  [[maybe_unused]] auto taskCount =
    static_cast<size_t>(iter.end() - iter.begin());
  // This will produce equivalent behavior:
  // auto results = co_await tmc::spawn_many(iter.begin(), taskCount);

  [[maybe_unused]] auto sum =
    std::accumulate(results.begin(), results.end(), 0);
  EXPECT_EQ(sum, (1 << N) - 1 - 8);
  // The results vector is right-sized
  EXPECT_EQ(results.size(), taskCount);
  EXPECT_EQ(results.size(), results.capacity());

  co_return;
}

template <int N>
tmc::task<void> spawn_many_each_dynamic_unknown_sized_iterator() {
  auto iter = iter_of_dynamic_unknown_size<N>();

  // Due to unpredictable_filter(), we cannot know the exact number of tasks.
  // We do not provide the N template parameter, and the size is unknown.

  // auto size = iter.end() - iter.begin(); // doesn't compile!

  // TooManyCooks will first internally construct a task vector (by appending /
  // reallocating as needed), and after the number of tasks has been determined,
  // a right-sized result vector will be constructed.
  std::vector<int> results;
  auto ts = tmc::spawn_many(iter.begin(), iter.end()).result_each();
  for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
    results.push_back(ts[idx]);
  }

  [[maybe_unused]] auto sum =
    std::accumulate(results.begin(), results.end(), 0);

  EXPECT_EQ(sum, (1 << N) - 1 - 8);
  // The result vector is right-sized; only the internal task vector is not
  EXPECT_EQ(results.size(), results.capacity());

  co_return;
}

template <int N> tmc::task<void> spawn_many_each_dynamic_bounded_iterator() {
  // In this example, we do not know the exact number of tasks that iter could
  // produce. The 3rd parameter MaxTasks serves as an upper bound on the number
  // of tasks that will be spawned. We also need to manually count the number of
  // tasks spawned. There are 2 sub-examples.
  size_t MaxTasks = N;
  {
    // Sub-Example 1: Iterator produces less than MaxTasks tasks.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });
    std::vector<int> results;
    auto ts = tmc::spawn_many(iter.begin(), iter.end(), MaxTasks).result_each();
    for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
      results.push_back(ts[idx]);
    }

    // At this point, taskCount == 4 and N == 5.
    EXPECT_EQ(taskCount, MaxTasks - 1);

    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    EXPECT_EQ(sum, (1 << N) - 1 - 8);
    // The results vector is still right-sized.
    EXPECT_EQ(results.size(), taskCount);
    EXPECT_EQ(results.size(), results.capacity());
  }
  {
    // Sub-Example 2: Iterator could produce more than MaxTasks tasks.
    // Only the first MaxTasks will be taken.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N + 20>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });
    std::vector<int> results;
    auto ts = tmc::spawn_many(iter.begin(), iter.end(), MaxTasks).result_each();
    for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
      results.push_back(ts[idx]);
    }

    // At this point, taskCount == 5 and N == 5.
    // We stopped consuming elements from the iterator after N tasks.
    EXPECT_EQ(taskCount, N);
    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    EXPECT_EQ(sum, (1 << N) - 1 - 8 + (1 << N));
    EXPECT_EQ(results.size(), taskCount);
  }
  co_return;
}

TEST_F(CATEGORY, spawn_many_each_static_sized_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_each_static_sized_iterator<5>();
  }());
}

TEST_F(CATEGORY, spawn_many_each_static_bounded_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_each_static_bounded_iterator<5>();
  }());
}

TEST_F(CATEGORY, spawn_many_each_dynamic_known_sized_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_each_dynamic_known_sized_iterator<5>();
  }());
}

TEST_F(CATEGORY, spawn_many_each_dynamic_unknown_sized_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_each_dynamic_unknown_sized_iterator<5>();
  }());
}

TEST_F(CATEGORY, spawn_many_each_dynamic_bounded_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_each_dynamic_bounded_iterator<5>();
  }());
}

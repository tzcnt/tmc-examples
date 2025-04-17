// This example show how to spawn and await tasks produced by an iterator.
// This example uses std::ranges, but they are not required. TooManyCooks
// supports several kinds of iterator pairs: begin() / end(), begin() / count,
// or C-style pointer / count

#define TMC_IMPL

#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <numeric>
#include <ranges>
#include <thread>
#include <vector>

constexpr int Count = 5;

tmc::task<int> work(int i) { co_return 1 << i; }
bool unpredictable_filter(int i) { return i != 3; }

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

template <int N> tmc::task<void> static_sized_iterator() {
  auto iter = iter_of_static_size<N>();
  // We know that the iterator produces exactly N tasks.
  // Provide the template parameter N to spawn_many, so that tasks and results
  // can be statically allocated in std::array.
  std::array<int, N> results = co_await tmc::spawn_many<N>(iter.begin());

  // This will produce equivalent behavior, but is not as explicit in the intent
  // auto results = co_await tmc::spawn_many<N>(iter.begin(), iter.end());

  [[maybe_unused]] auto sum =
    std::accumulate(results.begin(), results.end(), 0);

  assert(sum == (1 << N) - 1);

  co_return;
}

template <int N> tmc::task<void> static_bounded_iterator() {
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

    std::array<int, N> results =
      co_await tmc::spawn_many<N>(iter.begin(), iter.end());

    // At this point, taskCount == 4 and N == 5.
    // The last element of results will be left default-initialized.
    assert(taskCount == N - 1);

    // This extra work yields a performance benefit, because we can still use
    // std::array with an unknown-sized iterator that spawns "up to N" tasks.
    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    assert(sum == (1 << N) - 1 - 8);
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

    std::array<int, N> results =
      co_await tmc::spawn_many<N>(iter.begin(), iter.end());

    // At this point, taskCount == 5 and N == 5.
    // We stopped consuming elements from the iterator after N tasks.
    assert(taskCount == N);
    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    assert(sum == (1 << N) - 1 - 8 + (1 << N));
  }
  co_return;
}

template <int N> tmc::task<void> dynamic_known_sized_iterator() {
  auto iter = iter_of_dynamic_known_size<N>();
  [[maybe_unused]] auto taskCount =
    static_cast<size_t>(iter.end() - iter.begin());

  // The template parameter N to spawn_many is not provided.
  // This overload will produce a right-sized output vector
  // (internally calculated from tasks.end() - tasks.begin())

  // These invocations are all equivalent:
  // auto results = co_await tmc::spawn_many(iter.begin(), taskCount);
  // auto results = co_await tmc::spawn_many(iter.begin(), iter.end());
  std::vector<int> results = co_await tmc::spawn_many(iter);

  [[maybe_unused]] auto sum =
    std::accumulate(results.begin(), results.end(), 0);
  assert(sum == (1 << N) - 1 - 8);
  // The results vector is right-sized
  assert(results.size() == taskCount);
  assert(results.size() == results.capacity());

  co_return;
}

template <int N> tmc::task<void> dynamic_unknown_sized_iterator() {
  auto iter = iter_of_dynamic_unknown_size<N>();

  // Due to unpredictable_filter(), we cannot know the exact number of tasks.
  // We do not provide the N template parameter, and the size is unknown.

  // auto size = iter.end() - iter.begin(); // doesn't compile!

  // TooManyCooks will first internally construct a task vector (by appending /
  // reallocating as needed), and after the number of tasks has been determined,
  // a right-sized result vector will be constructed.

  // These invocations are equivalent:
  // auto results = co_await tmc::spawn_many(iter.begin(), iter.end());
  std::vector<int> results = co_await tmc::spawn_many(iter);

  [[maybe_unused]] auto sum =
    std::accumulate(results.begin(), results.end(), 0);

  assert(sum == (1 << N) - 1 - 8);
  // The result vector is right-sized; only the internal task vector is not
  assert(results.size() == results.capacity());

  co_return;
}

template <int N> tmc::task<void> dynamic_bounded_iterator() {
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

    std::vector<int> results =
      co_await tmc::spawn_many(iter.begin(), iter.end(), MaxTasks);

    // At this point, taskCount == 4 and N == 5.
    assert(taskCount == MaxTasks - 1);

    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    assert(sum == (1 << N) - 1 - 8);
    // The results vector is still right-sized.
    assert(results.size() == taskCount);
    assert(results.size() == results.capacity());
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

    std::vector<int> results =
      co_await tmc::spawn_many(iter.begin(), iter.end(), MaxTasks);

    // At this point, taskCount == 5 and N == 5.
    // We stopped consuming elements from the iterator after N tasks.
    assert(taskCount == N);
    [[maybe_unused]] auto sum =
      std::accumulate(results.begin(), results.begin() + taskCount, 0);
    assert(sum == (1 << N) - 1 - 8 + (1 << N));
    assert(results.size() == taskCount);
    assert(results.size() == results.capacity());
  }
  co_return;
}

// Test detach() when the maxCount is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_less() {
  std::atomic<int> counter(0);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many(tasks.begin(), tasks.end(), 5).detach();

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 5);
  co_return;
}

// Test detach() when the maxCount is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_greater() {
  std::atomic<int> counter(0);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many(tasks.begin(), tasks.end(), 15).detach();

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 10);
  co_return;
}

// Test detach() when the Count is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_less() {
  std::atomic<int> counter(0);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many<5>(tasks.begin(), tasks.end()).detach();

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 5);
  co_return;
}

// Test detach() when the Count is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_greater() {
  std::atomic<int> counter(0);

  auto tasks =
    std::ranges::views::iota(0, 10) |
    std::ranges::views::transform([&counter](int) -> tmc::task<void> {
      return [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        co_return;
      }(counter);
    });

  tmc::spawn_many<15>(tasks.begin(), tasks.end()).detach();

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 10);
  co_return;
}

// Test detach() when the maxCount is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_less_uncountable_iter() {
  std::atomic<int> counter(0);

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

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 5);
  co_return;
}

// Test detach() when the maxCount is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_greater_uncountable_iter() {
  std::atomic<int> counter(0);

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

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 9);
  co_return;
}

// Test detach() when the Count is less than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_less_uncountable_iter() {
  std::atomic<int> counter(0);

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

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 5);
  co_return;
}

// Test detach() when the Count is greater than the number of tasks in the
// iterator.
tmc::task<void> detach_maxCount_template_greater_uncountable_iter() {
  std::atomic<int> counter(0);

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

  // Wait for tasks to complete
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  assert(counter.load() == 9);
  co_return;
}

int main() {
  return tmc::async_main([]() -> tmc::task<int> {
    co_await static_sized_iterator<Count>();
    co_await static_bounded_iterator<Count>();
    co_await dynamic_known_sized_iterator<Count>();
    co_await dynamic_unknown_sized_iterator<Count>();
    co_await dynamic_bounded_iterator<Count>();
    co_await detach_maxCount_less();
    co_await detach_maxCount_greater();
    co_await detach_maxCount_template_less();
    co_await detach_maxCount_template_greater();

    co_await detach_maxCount_less_uncountable_iter();
    co_await detach_maxCount_greater_uncountable_iter();
    co_await detach_maxCount_template_less_uncountable_iter();
    co_await detach_maxCount_template_greater_uncountable_iter();

    co_return 0;
  }());
}

// This example show how to spawn and await tasks produced by an iterator.
// This example uses std::ranges, but they are not required. TooManyCooks
// supports several kinds of iterator pairs: begin() / end(), begin() / count,
// or C-style pointer / count

#define TMC_IMPL

#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/task.hpp"
#include <numeric>
#include <ranges>
#include <vector>

constexpr int Count = 5;

tmc::task<int> work(int i) { co_return 1 << i; }
bool unpredictable_filter(int i) { return i != 3; }

// This iterator produces exactly N tasks.
template <int N> auto iter_of_static_size() {
  return std::ranges::views::iota(0, N) | std::ranges::views::transform(work);
}

// This iterator produces at most N tasks, but maybe less.
template <int N> auto iter_of_static_bounded_size() {
  return std::ranges::views::iota(0, N) |
         std::ranges::views::filter(unpredictable_filter) |
         std::ranges::views::transform(work);
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

  auto sum = std::accumulate(results.begin(), results.end(), 0);

  assert(sum == (1 << N) - 1);

  co_return;
}

template <int N> tmc::task<void> static_bounded_iterator() {
  size_t taskCount = 0;
  auto iter = iter_of_static_bounded_size<N>() |
              std::ranges::views::transform([&taskCount](auto t) {
                ++taskCount;
                return t;
              });

  // Due to unpredictable_filter(), we cannot know the exact number of tasks,
  // but we know that it will be at most N. Provide the template parameter
  // N, so that tasks and results can be statically allocated in std::array.
  std::array<int, N> results =
    co_await tmc::spawn_many<N>(iter.begin(), iter.end());

  // We will get taskCount results, but the array is of size N.
  // Elements at results[taskCount..N-1] will be default-initialized.

  // We also needed to keep track of the number of tasks spawned manually.
  // This extra work yields a performance benefit, because we can still use
  // std::array even with an unknown-sized iterator.
  auto sum = std::accumulate(results.begin(), results.begin() + taskCount, 0);
  assert(sum == (1 << N) - 1 - 8);

  co_return;
}

template <int N> tmc::task<void> dynamic_known_sized_iterator() {
  auto iter = iter_of_dynamic_known_size<N>();

  // The template parameter N to spawn_many is not provided.
  // This overload will produce a right-sized output vector
  // (internally calculated from tasks.end() - tasks.begin())
  std::vector<int> results = co_await tmc::spawn_many(iter.begin(), iter.end());

  // This will produce equivalent behavior:
  // auto size = iter.end() - iter.begin();
  // auto results = co_await tmc::spawn_many(iter.begin(), size);

  auto sum = std::accumulate(results.begin(), results.end(), 0);
  assert(sum == (1 << N) - 1 - 8);
  // The results vector is right-sized
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
  std::vector<int> results = co_await tmc::spawn_many(iter.begin(), iter.end());

  auto sum = std::accumulate(results.begin(), results.end(), 0);

  assert(sum == (1 << N) - 1 - 8);
  // The result vector is right-sized; only the internal task vector is not
  assert(results.capacity() == results.size());

  co_return;
}

int main(int argc, char* argv[]) {
  return tmc::async_main([]() -> tmc::task<int> {
    co_await static_sized_iterator<Count>();
    co_await static_bounded_iterator<Count>();
    co_await dynamic_known_sized_iterator<Count>();
    co_await dynamic_unknown_sized_iterator<Count>();

    co_return 0;
  }());
}

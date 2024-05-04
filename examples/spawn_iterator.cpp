// This example show how to spawn and await tasks produced by an iterator.

#define TMC_IMPL

#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/sync.hpp"
#include "tmc/task.hpp"
#include "tmc/utils.hpp"
#include <numeric>
#include <ranges>
#include <vector>

constexpr size_t Count = 5;

tmc::task<int> work(int i) { co_return 1 << i; }
bool unpredictable_filter(int i) { return i != 3; }

// This iterator always produces exactly N tasks.
template <size_t N> auto iter_of_fixed_size() {
  return std::ranges::views::iota(0UL, N) | std::ranges::views::transform(work);
}

// This iterator produces up to N tasks.
template <size_t N> auto iter_of_dynamic_size() {
  return std::ranges::views::iota(0UL, N) |
         std::ranges::views::filter(unpredictable_filter) |
         std::ranges::views::transform(work);
}

template <size_t N> tmc::task<void> static_sized_iterator() {
  auto iter = iter_of_fixed_size<N>();
  // We know that the iterator produces exactly N tasks.
  // Provide the template parameter N, so that tasks and results can be
  // statically allocated in std::array.
  std::array<int, N> results =
    co_await tmc::spawn_many<N>(iter.begin(), iter.end());
  auto sum = std::accumulate(results.begin(), results.end(), 0);

  assert(sum == (1 << N) - 1);

  co_return;
}

template <size_t N> tmc::task<void> dynamic_sized_vector_iterator() {
  auto iter = iter_of_dynamic_size<N>();
  // We do not know the exact size of iter, so we
  // collect it into a std::vector first.
  std::vector<tmc::task<int>> tasks =
    std::vector<tmc::task<int>>(iter.begin(), iter.end());

  // This overload will produce a right-sized output
  // (internally calculated from tasks.end() - tasks.begin())
  std::vector<int> results =
    co_await tmc::spawn_many(tasks.begin(), tasks.end());

  // These overloads will behave identically
  // auto results = co_await tmc::spawn_many<N>(tasks.begin(), N);
  // auto results = co_await tmc::spawn_many<N>(tasks.data(), tasks.data() + N);

  auto sum = std::accumulate(results.begin(), results.end(), 0);
  assert(sum == (1 << N) - 1 - 8);
  assert(results.size() == tasks.size());
  assert(results.capacity() == tasks.size());

  co_return;
}

template <size_t N> tmc::task<void> bounded_size_iterator() {
  size_t taskCount = 0;
  auto iter = std::ranges::views::iota(0UL, N) |
              std::ranges::views::filter(unpredictable_filter) |
              std::ranges::views::transform([&taskCount](int i) {
                ++taskCount;
                return work(i);
              });

  // Due to unpredictable_filter(), we cannot know the exact number of tasks,
  // but we know that it will be at most N. Provide the template parameter
  // N, so that tasks and results can be statically allocated in std::array.
  std::array<int, N> results =
    co_await tmc::spawn_many<N>(iter.begin(), iter.end());

  // We also needed to keep track of the number of tasks spawned manually.
  // This extra work yields a performance benefit, because we can still use
  // std::array even with an unknown-sized iterator.
  auto sum = std::accumulate(results.begin(), results.begin() + taskCount, 0);
  assert(sum == (1 << N) - 1 - 8);

  co_return;
}

template <size_t N> tmc::task<void> dynamic_sized_iterator() {
  auto iter = iter_of_dynamic_size<N>();

  // Due to unpredictable_filter(), we cannot know the exact number of tasks.
  // We do not provide the N template parameter, and the size cannot be
  // calculated from (iter.end() - iter.begin()). The framework will use a
  // std::vector without preallocation.
  std::vector<int> results = co_await tmc::spawn_many(iter.begin(), iter.end());

  auto sum = std::accumulate(results.begin(), results.end(), 0);

  assert(sum == (1 << N) - 1 - 8);

  co_return;
}

int main(int argc, char* argv[]) {
  return tmc::async_main([]() -> tmc::task<int> {
    // Iterator produces Count tasks
    co_await static_sized_iterator<Count>();

    // Iterator produces at most Count tasks
    // 2 ways to solve it
    co_await dynamic_sized_vector_iterator<Count>();
    co_await bounded_size_iterator<Count>();

    // Iterator produces unknown number of tasks
    // A small performance penalty is paid - no preallocation
    co_await dynamic_sized_iterator<Count>();

    co_return 0;
  }());
}

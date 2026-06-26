#pragma once

#include "test_common.hpp"

#include <ranges>
#include <vector>

// tests ported from examples/spawn_iterator.cpp

static inline tmc::task<int> work(int i) { co_return 1 << i; }

// This iterator produces exactly N tasks.
template <int N> auto iter_of_static_size() {
  return std::ranges::views::iota(0, N) | std::ranges::views::transform(work);
}

// This iterator produces a dynamic number of tasks,
// which can be calculated by the caller in O(1) time by
// `return.end() - return.begin()`
// (it's the same set of tasks as the unknown iterator, but collected into a vector, which
// allows for size calculation)
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

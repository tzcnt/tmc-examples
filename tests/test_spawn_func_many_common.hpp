#pragma once

#include "test_common.hpp"

#include <ranges>
#include <vector>

// tests ported from examples/spawn_iterator.cpp

static inline int func_work(int i) { return 1 << i; }

// This iterator produces exactly N tasks.
template <int N> auto func_iter_of_static_size() {
  return std::ranges::views::iota(0, N) |
         std::ranges::views::transform([](int i) -> auto {
           return [i]() -> int { return func_work(i); };
         });
}

// This iterator produces a dynamic number of tasks,
// which can be calculated by the caller in O(1) time by
// `return.end() - return.begin()`
template <int N> auto func_iter_of_dynamic_known_size() {
  auto iter = std::ranges::views::iota(0, N) |
              std::ranges::views::filter(unpredictable_filter) |
              std::ranges::views::transform([](int i) -> auto {
                return [i]() -> int { return func_work(i); };
              });
  return std::vector(iter.begin(), iter.end());
}

// This iterator produces a dynamic number of tasks,
// and does not support O(1) size calculation;
// `return.end() - return.begin()` will not compile.
template <int N> auto func_iter_of_dynamic_unknown_size() {
  return std::ranges::views::iota(0, N) |
         std::ranges::views::filter(unpredictable_filter) |
         std::ranges::views::transform([](int i) -> auto {
           return [i]() -> int { return func_work(i); };
         });
}

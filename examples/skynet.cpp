// An implementation of the skynet benchmark as described here:
// https://github.com/atemerev/skynet

#include "tmc/aw_yield.hpp"
#include "tmc/current.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_group.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <chrono>
#include <cstdio>
#include <ranges>

// The proper sum of skynet (1M tasks) is 499999500000.
// 32-bit platforms can't hold the full sum, but unsigned integer overflow is
// defined so it will wrap to this number.
static constexpr inline size_t EXPECTED_RESULT =
  sizeof(size_t) == 8 ? static_cast<size_t>(499999500000)
                      : static_cast<size_t>(1783293664);

#define DEPTH 6

tmc::task<size_t> skynet_leaf(size_t BaseNum) { co_return BaseNum; }

template <size_t DepthMax>
tmc::task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
  // if (Depth == DepthMax) {
  //   co_return BaseNum;
  // }
  // else if (Depth == DepthMax - 2) {
  //   co_await tmc::pin_to_thread();
  // }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }

  // std::array<size_t, 10> results = co_await tmc::spawn_many<10>(
  //   (
  //     std::ranges::views::iota(0UL) |
  //     std::ranges::views::transform([=](size_t idx) {
  //       return skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
  //     })
  //   ).begin()
  // );

  std::array<size_t, 10> results;
  if (Depth == DepthMax - 1) {
    auto s = tmc::spawn_group<10, tmc::task<size_t>>();
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 0));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 1));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 2));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 3));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 4));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 5));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 6));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 7));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 8));
    co_await s.add_clang(skynet_leaf(BaseNum + depthOffset * 9));
    results = co_await std::move(s);
  } else {
    auto tasks =
      std::ranges::views::iota(0UL) |
      std::ranges::views::transform([=](size_t idx) {
        return skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
      });
    auto s = tmc::spawn_many<10>(tasks.begin());

    if (Depth == DepthMax - 2) {
      results = co_await std::move(s).run_on(
        tmc::cpu_executor().single_thread(tmc::current_thread_index())
      );
    } else {
      results = co_await std::move(s);
    }
  }

  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t DepthMax> tmc::task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != EXPECTED_RESULT) {
    std::printf("%zu\n", count);
  }
}

template <size_t Depth> tmc::task<void> loop_skynet() {
  const size_t iter_count = 1000;
  for (size_t j = 0; j < 1; ++j) {
    auto startTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iter_count; ++i) {
      co_await skynet<Depth>();
    }
    auto endTime = std::chrono::high_resolution_clock::now();

    size_t execDur = static_cast<size_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
        .count()
    );
    std::printf("%zu skynet iterations in %zu us\n", iter_count, execDur);
  }
}

int main() {
  std::printf("Running skynet benchmark x1000...\n");
  tmc::cpu_executor()
    // This specific benchmark performs better with LATTICE_MATRIX due to its
    // high degree of nested parallelism.
    .set_work_stealing_strategy(tmc::work_stealing_strategy::LATTICE_MATRIX)
    .init();
  return tmc::async_main([]() -> tmc::task<int> {
    co_await loop_skynet<DEPTH>();
    co_return 0;
  }());
}

// An implementation of the skynet benchmark as described here:
// https://github.com/atemerev/skynet

#define TMC_IMPL

#include "tmc/ex_cpu.hpp"
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

template <size_t DepthMax>
tmc::task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
  if (Depth == DepthMax) {
    co_return BaseNum;
  }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }

  // // Simplest way to spawn subtasks
  // std::array<tmc::task<size_t>, 10> children;
  // for (size_t idx = 0; idx < 10; ++idx) {
  //   children[idx] =
  //     skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
  // }
  // std::array<size_t, 10> results =
  //   co_await tmc::spawn_many<10>(children.data());

  // Construction from a sized iterator has slightly better performance
  std::array<size_t, 10> results = co_await tmc::spawn_many<10>(
    (
      std::ranges::views::iota(0UL) |
      std::ranges::views::transform([=](size_t idx) {
        return skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
      })
    ).begin()
  );

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
  for (size_t j = 0; j < 5; ++j) {
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

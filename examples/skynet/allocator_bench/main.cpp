#define TMC_IMPL

#include "build/bench_config.hpp"

#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <chrono>
#include <cstdio>
#include <ranges>

// 32-bit platform can't hold the full sum, but signed integer overflow is
// defined so it will wrap to this number.
static constexpr inline size_t EXPECTED_RESULT =
  TMC_PLATFORM_BITS == 64 ? 499999500000 : 1783293664;

using namespace tmc;

namespace skynet {
namespace coro {
namespace bulk {
// all tasks are spawned at the same priority
template <size_t DepthMax>
task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
  if (Depth == DepthMax) {
    co_return BaseNum;
  }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }

  std::array<size_t, 10> results = co_await tmc::spawn_many<10>(
    (std::ranges::views::iota(0UL) |
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
template <size_t DepthMax> task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != EXPECTED_RESULT) {
    std::printf("%zu\n", count);
  }
}
} // namespace bulk
} // namespace coro
} // namespace skynet

constexpr size_t NRUNS = 100;

tmc::task<int> bench_async_main() {
  // these time points are matched to the original bench locations
  auto startTime = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < NRUNS; ++i) {
    co_await skynet::coro::bulk::skynet<6>();
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  size_t totalTimeUs =
    std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
      .count();
  std::printf("%zu", totalTimeUs / NRUNS);

  co_return 0;
}
int main() {
  tmc::cpu_executor().set_thread_count(NTHREADS).init();
  return tmc::async_main(bench_async_main());
}

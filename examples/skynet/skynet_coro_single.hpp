#pragma once
#include "skynet_shared.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn.hpp"
#include "tmc/sync.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>

namespace skynet {
namespace coro {
namespace single {
static std::atomic_bool done;
// all tasks are spawned at the same priority
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
  for (size_t idx = 0; idx < 10; ++idx) {
    count += co_await tmc::spawn(
      skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1)
    );
  }
  co_return count;
}

template <size_t DepthMax> tmc::task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != EXPECTED_RESULT) {
    std::printf("%zu\n", count);
  }
  done.store(true);
}

template <size_t Depth = 6> void run_skynet() {
  done.store(false);
  tmc::ex_cpu executor;
  executor.init();
  auto startTime = std::chrono::high_resolution_clock::now();
  auto future = tmc::post_waitable(executor, skynet<Depth>(), 0);
  future.wait();
  auto endTime = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_single did not finish!\n");
  }

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime)
      .count();
  std::printf(
    "executed skynet in %zu ns: %zu thread-ns\n", execDur,
    executor.thread_count() * execDur
  );
}

} // namespace single
} // namespace coro
} // namespace skynet

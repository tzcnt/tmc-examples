#pragma once
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/sync.hpp"
#include "tmc/utils.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>

namespace skynet {
namespace coro {
namespace bulk {
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

  /// Simplest way to spawn subtasks
  // std::array<tmc::task<size_t>, 10> children;
  // for (size_t idx = 0; idx < 10; ++idx) {
  //   children[idx] = skynet_one<DepthMax>(BaseNum + depthOffset * idx,
  //   Depth + 1);
  // }
  // std::array<size_t, 10> results = co_await
  // tmc::spawn_many<10>(children.data());

  /// Concise and slightly faster way to run subtasks
  auto spm = tmc::spawn_many<10>(tmc::iter_adapter(
    0ULL,
    [=](size_t idx) -> tmc::task<size_t> {
      return skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
    }
  ));

  for (auto& t : co_await spm) {
    count +=
      tmc::task<size_t>::from_address(t.address()).promise().result_ref();
  }
  co_return count;
}
template <size_t DepthMax> tmc::task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != 499999500000) {
    std::printf("%" PRIu64 "\n", count);
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
    std::printf("skynet_coro_bulk did not finish!\n");
  }

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
  std::printf(
    "executed skynet in %" PRIu64 " ns: %" PRIu64 " thread-ns\n",
    execDur.count(),
    executor.thread_count() * static_cast<size_t>(execDur.count())
  );
}

} // namespace bulk
} // namespace coro
} // namespace skynet

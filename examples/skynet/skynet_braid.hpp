#pragma once
#include "skynet_shared.hpp"
#include "tmc/ex_braid.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/sync.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>

namespace skynet {
namespace braids {
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
  tmc::ex_braid br(executor);
  auto startTime = std::chrono::high_resolution_clock::now();
  auto future = tmc::post_waitable(br, skynet<Depth>(), 0);
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
  std::array<tmc::task<size_t>, 10> children;
  for (size_t idx = 0; idx < 10; ++idx) {
    children[idx] =
      skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
  }
  std::array<size_t, 10> results =
    co_await tmc::spawn_many<10>(children.data());
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t DepthMax> tmc::task<void> skynet() {
  tmc::ex_braid br;
  // TODO implement spawn with executor parameter so we don't have to do this
  // also need eager execution / delayed await since each task goes on a diff
  // exec
  size_t count = co_await [](tmc::ex_braid* braid_ptr) -> tmc::task<size_t> {
    co_await tmc::enter(braid_ptr);
    co_return co_await skynet_one<DepthMax>(0, 0);
  }(&br);
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
    std::printf("skynet_coro_bulk did not finish!\n");
  }

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime)
      .count();
  std::printf(
    "executed skynet in %zu ns: %zu thread-ns\n", execDur,
    executor.thread_count() * execDur
  );
}
} // namespace bulk

namespace fork {
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
  std::array<tmc::task<size_t>, 10> children;
  for (size_t idx = 0; idx < 10; ++idx) {
    children[idx] =
      skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
  }
  std::array<size_t, 10> results =
    co_await tmc::spawn_many<10>(children.data());
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t DepthMax> tmc::task<void> skynet() {
  std::array<tmc::task<size_t>, 10> children;
  std::array<tmc::ex_braid, 10> braids;
  for (size_t i = 0; i < 10; ++i) {
    // TODO implement spawn with executor parameter so we don't have to do
    // this also need eager execution / delayed await since each task goes on a
    // diff exec
    children[i] =
      [](size_t i_in, tmc::ex_braid* braid_ptr) -> tmc::task<size_t> {
      co_await tmc::enter(braid_ptr);
      co_return co_await skynet_one<DepthMax>(100000 * i_in, 1);
    }(i, &braids[i]);
  }
  std::array<size_t, 10> results =
    co_await tmc::spawn_many<10>(children.data());
  size_t count = 0;
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
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
    std::printf("skynet_coro_bulk did not finish!\n");
  }

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime)
      .count();
  std::printf(
    "executed skynet in %zu ns: %zu thread-ns\n", execDur,
    executor.thread_count() * execDur
  );
}
} // namespace fork
} // namespace braids
} // namespace skynet

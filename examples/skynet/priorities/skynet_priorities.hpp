#pragma once
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task.hpp"
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
namespace prio_asc {
static std::atomic_bool done;
// child tasks are spawned with ascending priority number (lower priority)
template <size_t DepthMax>
static tmc::task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
  if (Depth == DepthMax) {
    co_return BaseNum;
  }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }
  std::array<size_t, 10> results =
    co_await tmc::spawn_many<10>(
      tmc::iter_adapter(
        0ULL,
        [=](size_t idx) -> tmc::task<size_t> {
          return skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
        }
      )
    ).with_priority(Depth + 1);
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}

template <size_t DepthMax> static tmc::task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != 499999500000) {
    std::printf("%" PRIu64 "\n", count);
  }
  done.store(true);
}

template <size_t Depth = 6> static void run_skynet() {
  done.store(false);
  static_assert(Depth <= 6);
  tmc::ex_cpu executor;
  executor.set_priority_count(Depth + 1).init();
  auto startTime = std::chrono::high_resolution_clock::now();
  auto future = tmc::post_waitable(executor, skynet<Depth>(), 0);
  future.wait();
  auto endTime = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_bulk_asc did not finish!\n");
  }

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
  std::printf(
    "executed skynet in %" PRIu64 " ns: %" PRIu64 " thread-ns\n",
    execDur.count(),
    executor.thread_count() * static_cast<size_t>(execDur.count())
  );
}
} // namespace prio_asc

namespace prio_desc {
static std::atomic_bool done;
// child tasks are spawned with descending priority number (higher priority)
template <size_t DepthMax>
static tmc::task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
  if (Depth == DepthMax) {
    co_return BaseNum;
  }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }
  std::array<size_t, 10> results =
    co_await tmc::spawn_many<10>(
      tmc::iter_adapter(
        0ULL,
        [=](size_t idx) -> tmc::task<size_t> {
          return skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
        }
      )
    ).with_priority(DepthMax - Depth - 1);
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}

template <size_t DepthMax> static tmc::task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != 499999500000) {
    std::printf("%" PRIu64 "\n", count);
  }
  done.store(true);
}
template <size_t Depth = 6> static void run_skynet() {
  done.store(false);
  static_assert(Depth <= 6);
  tmc::ex_cpu executor;
  executor.set_priority_count(Depth + 1).init();
  auto startTime = std::chrono::high_resolution_clock::now();
  auto future = tmc::post_waitable(executor, skynet<Depth>(), Depth);
  future.wait();
  auto endTime = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_bulk_desc did not finish!\n");
  }

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
  std::printf(
    "executed skynet in %" PRIu64 " ns: %" PRIu64 " thread-ns\n",
    execDur.count(),
    executor.thread_count() * static_cast<size_t>(execDur.count())
  );
}
} // namespace prio_desc
} // namespace bulk
namespace single {
namespace prio_asc {
static std::atomic_bool done;
// child tasks are spawned with ascending priority number (lower priority)
template <size_t DepthMax>
static tmc::task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
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
    )
               .with_priority(Depth + 1);
  }
  co_return count;
}

template <size_t DepthMax> static tmc::task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != 499999500000) {
    std::printf("%" PRIu64 "\n", count);
  }
  done.store(true);
}

template <size_t Depth> static void run_skynet() {
  done.store(false);
  static_assert(Depth <= 6);
  tmc::ex_cpu executor;
  executor.set_priority_count(Depth + 1).init();
  auto startTime = std::chrono::high_resolution_clock::now();
  auto future = tmc::post_waitable(executor, skynet<Depth>(), 0);
  future.wait();
  auto endTime = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_single_asc did not finish!\n");
  }

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
  std::printf(
    "executed skynet in %" PRIu64 " ns: %" PRIu64 " thread-ns\n",
    execDur.count(),
    executor.thread_count() * static_cast<size_t>(execDur.count())
  );
}
} // namespace prio_asc

namespace prio_desc {
static std::atomic_bool done;
// child tasks are spawned with descending priority number (higher priority)
template <size_t DepthMax>
static tmc::task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
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
    )
               .with_priority(DepthMax - Depth - 1);
  }
  co_return count;
}

template <size_t DepthMax> static tmc::task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  if (count != 499999500000) {
    std::printf("%" PRIu64 "\n", count);
  }
  done.store(true);
}

template <size_t Depth> static void run_skynet() {
  done.store(false);
  static_assert(Depth <= 6);
  tmc::ex_cpu executor;
  executor.set_priority_count(Depth + 1).init();
  auto startTime = std::chrono::high_resolution_clock::now();
  auto future = tmc::post_waitable(executor, skynet<Depth>(), Depth);
  future.wait();
  auto endTime = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_single_desc did not finish!\n");
  }

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
  std::printf(
    "executed skynet in %" PRIu64 " ns: %" PRIu64 " thread-ns\n",
    execDur.count(),
    executor.thread_count() * static_cast<size_t>(execDur.count())
  );
}
} // namespace prio_desc
} // namespace single
} // namespace coro
} // namespace skynet

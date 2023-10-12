#pragma once
#include "tmc/all_headers.hpp"
#include <atomic>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <thread>
using namespace tmc;

namespace skynet {
namespace coro {
namespace bulk {
namespace prio_asc {
std::atomic_bool done;
// child tasks are spawned with ascending priority number (lower priority)
template <size_t depth_max>
static task<size_t> skynet_one(size_t base_num, size_t local_depth) {
  if (local_depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - local_depth - 1; ++i) {
    depth_offset *= 10;
  }
  std::array<size_t, 10> results =
      co_await spawn_many<10>(iter_adapter(0, [=](size_t idx) -> task<size_t> {
        return skynet_one<depth_max>(base_num + depth_offset * idx,
                                     local_depth + 1);
      })).with_priority(local_depth + 1);
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}

template <size_t depth_max> static task<void> skynet() {
  size_t count = co_await skynet_one<depth_max>(0, 0);
  if (count != 499999500000) {
    std::printf("%ld\n", count);
  }
  done.store(true);
}

template <size_t depth = 6> static void run_skynet() {
  done.store(false);
  static_assert(depth <= 6);
  ex_cpu executor;
  executor.set_priority_count(depth + 1).init();
  auto start_time = std::chrono::high_resolution_clock::now();
  auto future = post_waitable(executor, skynet<depth>(), 0);
  future.wait();
  auto end_time = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_bulk_asc did not finish!\n");
  }

  auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time);
  std::printf("executed skynet in %ld ns: %ld thread-ns\n", exec_dur.count(),
              executor.thread_count() * exec_dur.count());
}
} // namespace prio_asc

namespace prio_desc {
std::atomic_bool done;
// child tasks are spawned with descending priority number (higher priority)
template <size_t depth_max>
static task<size_t> skynet_one(size_t base_num, size_t local_depth) {
  if (local_depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - local_depth - 1; ++i) {
    depth_offset *= 10;
  }
  std::array<size_t, 10> results =
      co_await spawn_many<10>(iter_adapter(0, [=](size_t idx) -> task<size_t> {
        return skynet_one<depth_max>(base_num + depth_offset * idx,
                                     local_depth + 1);
      })).with_priority(depth_max - local_depth - 1);
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}

template <size_t depth_max> static task<void> skynet() {
  size_t count = co_await skynet_one<depth_max>(0, 0);
  if (count != 499999500000) {
    std::printf("%ld\n", count);
  }
  done.store(true);
}
template <size_t depth = 6> static void run_skynet() {
  done.store(false);
  static_assert(depth <= 6);
  ex_cpu executor;
  executor.set_priority_count(depth + 1).init();
  auto start_time = std::chrono::high_resolution_clock::now();
  auto future = post_waitable(executor, skynet<depth>(), depth);
  future.wait();
  auto end_time = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_bulk_desc did not finish!\n");
  }

  auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time);
  std::printf("executed skynet in %ld ns: %ld thread-ns\n", exec_dur.count(),
              executor.thread_count() * exec_dur.count());
}
} // namespace prio_desc
} // namespace bulk
namespace single {
namespace prio_asc {
std::atomic_bool done;
// child tasks are spawned with ascending priority number (lower priority)
template <size_t depth_max>
static task<size_t> skynet_one(size_t base_num, size_t local_depth) {
  if (local_depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - local_depth - 1; ++i) {
    depth_offset *= 10;
  }
  for (size_t idx = 0; idx < 10; ++idx) {
    count += co_await spawn(skynet_one<depth_max>(base_num + depth_offset * idx,
                                                  local_depth + 1))
                 .with_priority(local_depth + 1);
  }
  co_return count;
}

template <size_t depth_max> static task<void> skynet() {
  size_t count = co_await skynet_one<depth_max>(0, 0);
  if (count != 499999500000) {
    std::printf("%ld\n", count);
  }
  done.store(true);
}

template <size_t depth> static void run_skynet() {
  done.store(false);
  static_assert(depth <= 6);
  ex_cpu executor;
  executor.set_priority_count(depth + 1).init();
  auto start_time = std::chrono::high_resolution_clock::now();
  auto future = post_waitable(executor, skynet<depth>(), 0);
  future.wait();
  auto end_time = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_single_asc did not finish!\n");
  }

  auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time);
  std::printf("executed skynet in %ld ns: %ld thread-ns\n", exec_dur.count(),
              executor.thread_count() * exec_dur.count());
}
} // namespace prio_asc

namespace prio_desc {
std::atomic_bool done;
// child tasks are spawned with descending priority number (higher priority)
template <size_t depth_max>
static task<size_t> skynet_one(size_t base_num, size_t local_depth) {
  if (local_depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - local_depth - 1; ++i) {
    depth_offset *= 10;
  }
  for (size_t idx = 0; idx < 10; ++idx) {
    count += co_await spawn(skynet_one<depth_max>(base_num + depth_offset * idx,
                                                  local_depth + 1))
                 .with_priority(depth_max - local_depth - 1);
  }
  co_return count;
}

template <size_t depth_max> static task<void> skynet() {
  size_t count = co_await skynet_one<depth_max>(0, 0);
  if (count != 499999500000) {
    std::printf("%ld\n", count);
  }
  done.store(true);
}

template <size_t depth> static void run_skynet() {
  done.store(false);
  static_assert(depth <= 6);
  ex_cpu executor;
  executor.set_priority_count(depth + 1).init();
  auto start_time = std::chrono::high_resolution_clock::now();
  auto future = post_waitable(executor, skynet<depth>(), depth);
  future.wait();
  auto end_time = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_single_desc did not finish!\n");
  }

  auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time);
  std::printf("executed skynet in %ld ns: %ld thread-ns\n", exec_dur.count(),
              executor.thread_count() * exec_dur.count());
}
} // namespace prio_desc
} // namespace single
} // namespace coro
} // namespace skynet

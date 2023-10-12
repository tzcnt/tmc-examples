#pragma once
#include "tmc/aw_yield.hpp"
#include "tmc/ex_braid.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_func.hpp"
#include "tmc/spawn_task.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/sync.hpp"
#include "tmc/task.hpp"
#include <atomic>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;
using namespace std;
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace tmc;

namespace skynet {
namespace braids {
namespace single {
std::atomic_bool done;
// all tasks are spawned at the same priority
template <size_t depth_max>
task<size_t> skynet_one(size_t base_num, size_t depth) {
  if (depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - depth - 1; ++i) {
    depth_offset *= 10;
  }
  for (size_t idx = 0; idx < 10; ++idx) {
    count += co_await spawn(
        skynet_one<depth_max>(base_num + depth_offset * idx, depth + 1));
  }
  co_return count;
}

template <size_t depth_max> task<void> skynet() {
  size_t count = co_await skynet_one<depth_max>(0, 0);
  if (count != 499999500000) {
    std::printf("%ld\n", count);
  }
  done.store(true);
}

template <size_t depth = 6> void run_skynet() {
  done.store(false);
  ex_cpu executor;
  executor.init();
  ex_braid br(executor);
  auto start_time = std::chrono::high_resolution_clock::now();
  auto future = post_waitable(br, skynet<depth>(), 0);
  future.wait();
  auto end_time = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_single did not finish!\n");
  }

  auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time);
  std::printf("executed skynet in %ld ns: %ld thread-ns\n", exec_dur.count(),
              executor.thread_count() * exec_dur.count());
}
} // namespace single

namespace bulk {
std::atomic_bool done;
// all tasks are spawned at the same priority
template <size_t depth_max>
task<size_t> skynet_one(size_t base_num, size_t depth) {
  if (depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - depth - 1; ++i) {
    depth_offset *= 10;
  }
  std::array<task<size_t>, 10> children;
  for (size_t idx = 0; idx < 10; ++idx) {
    children[idx] =
        skynet_one<depth_max>(base_num + depth_offset * idx, depth + 1);
  }
  std::array<size_t, 10> results = co_await spawn_many<10>(children.data());
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t depth_max> task<void> skynet() {
  ex_braid br;
  // TODO implement spawn with executor parameter so we don't have to do this
  // also need eager execution / delayed await since each task goes on a diff
  // exec
  size_t count = co_await [](ex_braid *braid_ptr) -> task<size_t> {
    co_await braid_ptr->enter();
    co_return co_await skynet_one<depth_max>(0, 0);
  }(&br);
  if (count != 499999500000) {
    std::printf("%ld\n", count);
  }
  done.store(true);
}

template <size_t depth = 6> void run_skynet() {
  done.store(false);
  ex_cpu executor;
  executor.init();
  auto start_time = std::chrono::high_resolution_clock::now();
  auto future = post_waitable(executor, skynet<depth>(), 0);
  future.wait();
  auto end_time = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_bulk did not finish!\n");
  }

  auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time);
  std::printf("executed skynet in %ld ns: %ld thread-ns\n", exec_dur.count(),
              executor.thread_count() * exec_dur.count());
}
} // namespace bulk

namespace fork {
std::atomic_bool done;
// all tasks are spawned at the same priority
template <size_t depth_max>
task<size_t> skynet_one(size_t base_num, size_t depth) {
  if (depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - depth - 1; ++i) {
    depth_offset *= 10;
  }
  std::array<task<size_t>, 10> children;
  for (size_t idx = 0; idx < 10; ++idx) {
    children[idx] =
        skynet_one<depth_max>(base_num + depth_offset * idx, depth + 1);
  }
  std::array<size_t, 10> results = co_await spawn_many<10>(children.data());
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t depth_max> task<void> skynet() {
  std::array<task<size_t>, 10> children;
  std::array<ex_braid, 10> braids;
  for (size_t i = 0; i < 10; ++i) {
    // TODO implement spawn with executor parameter so we don't have to do
    // this also need eager execution / delayed await since each task goes on a
    // diff exec
    children[i] = [](size_t i_in, ex_braid *braid_ptr) -> task<size_t> {
      co_await braid_ptr->enter();
      co_return co_await skynet_one<depth_max>(100000 * i_in, 1);
    }(i, &braids[i]);
  }
  std::array<size_t, 10> results = co_await spawn_many<10>(children.data());
  size_t count = 0;
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  if (count != 499999500000) {
    std::printf("%ld\n", count);
  }
  done.store(true);
}

template <size_t depth = 6> void run_skynet() {
  done.store(false);
  ex_cpu executor;
  executor.init();
  auto start_time = std::chrono::high_resolution_clock::now();
  auto future = post_waitable(executor, skynet<depth>(), 0);
  future.wait();
  auto end_time = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_bulk did not finish!\n");
  }

  auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
      end_time - start_time);
  std::printf("executed skynet in %ld ns: %ld thread-ns\n", exec_dur.count(),
              executor.thread_count() * exec_dur.count());
}
} // namespace fork
} // namespace braids
} // namespace skynet

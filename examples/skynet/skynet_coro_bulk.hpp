#pragma once
#include "tmc/all_headers.hpp"
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
namespace coro {
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

  // std::array<task<size_t>, 10> children;
  // for (size_t idx = 0; idx < 10; ++idx) {
  //   children[idx] = skynet_one<depth_max>(base_num + depth_offset * idx,
  //   depth + 1);
  // }
  // std::array<size_t, 10> results = co_await
  // spawn_many<10>(children.data());
  std::array<size_t, 10> results =
      co_await spawn_many<10>(iter_adapter(0, [=](size_t idx) -> task<size_t> {
        return skynet_one<depth_max>(base_num + depth_offset * idx, depth + 1);
      }));

  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
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

  executor.graceful_stop();
}

} // namespace bulk
} // namespace coro
} // namespace skynet

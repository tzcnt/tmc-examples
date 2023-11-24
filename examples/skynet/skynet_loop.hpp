#pragma once
#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/task.hpp"
#include <cinttypes>

template <size_t depth = 6> task<void> loop_skynet() {
  static_assert(depth <= 6);
  const size_t iter_count = 1000;
  for (size_t j = 0; j < 5; ++j) {
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iter_count; ++i) {
      // Different implementations of skynet below. The most efficient
      // implementation, coro::bulk, is the default.

      // co_await skynet::direct::skynet<depth>();
      // co_await skynet::func::single::skynet<depth>();
      // co_await skynet::coro::single::skynet<depth>();
      co_await skynet::coro::bulk::skynet<depth>();
      // co_await skynet::braids::single::skynet<depth>();

      // co_await skynet::braids::fork::skynet<depth>();

      // co_await skynet::braids::bulk::skynet<depth>();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto exec_dur = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time
    );
    std::printf(
      "%" PRIu64 " skynet iterations in %" PRIu64 " us: %" PRIu64
      " thread-us\n",
      iter_count, exec_dur.count(),
      tmc::cpu_executor().thread_count() * exec_dur.count()
    );
  }
}

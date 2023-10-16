#pragma once
#include "priorities/skynet_priorities.hpp"
#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/task.hpp"

template <size_t depth = 6> task<void> loop_skynet() {
  static_assert(depth <= 6);
  const size_t iter_count = 1000;
  for (size_t j = 0; j < 5; ++j) {
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iter_count; ++i) {
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

// These tests use additional priorities at different depths of the tree
template <size_t depth = 6> task<void> loop_skynet_prio() {
  static_assert(depth <= 6);
  const size_t iter_count = 10;
  for (size_t j = 0; j < 5; ++j) {
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iter_count; ++i) {
      // co_await skynet::coro::single::prio_asc::skynet<depth>();

      // co_await spawn(skynet::coro::single::prio_desc::skynet<depth>(),
      // depth);

      co_await skynet::coro::bulk::prio_asc::skynet<depth>();

      // co_await spawn(skynet::coro::bulk::prio_desc::skynet<depth>(), depth);
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
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

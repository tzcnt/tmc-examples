#pragma once
#include "priorities/skynet_priorities.hpp"
#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/task.hpp"
#include <cstdio>

template <size_t Depth = 6> tmc::task<void> loop_skynet() {
  static_assert(Depth <= 6);
  const size_t IterCount = 1000;
  for (size_t j = 0; j < 5; ++j) {
    auto startTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < IterCount; ++i) {
      // co_await skynet::direct::skynet<Depth>();
      // co_await skynet::func::single::skynet<Depth>();
      // co_await skynet::coro::single::skynet<Depth>();
      co_await skynet::coro::bulk::skynet<Depth>();
      // co_await skynet::braids::single::skynet<Depth>();

      // co_await skynet::braids::fork::skynet<Depth>();

      // co_await skynet::braids::bulk::skynet<Depth>();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto execDur = std::chrono::duration_cast<std::chrono::microseconds>(
      endTime - startTime
    );
    std::printf(
      "%" PRIu64 " skynet iterations in %" PRIu64 " us: %" PRIu64
      " thread-us\n",
      IterCount, execDur.count(),
      tmc::cpu_executor().thread_count() * execDur.count()
    );
  }
}

// These tests use additional priorities at different depths of the tree
template <size_t Depth = 6> tmc::task<void> loop_skynet_prio() {
  static_assert(Depth <= 6);
  const size_t IterCount = 10;
  for (size_t j = 0; j < 5; ++j) {
    auto startTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < IterCount; ++i) {
      // co_await skynet::coro::single::prio_asc::skynet<Depth>();

      // co_await tmc::spawn(skynet::coro::single::prio_desc::skynet<Depth>(),
      // Depth);

      co_await skynet::coro::bulk::prio_asc::skynet<Depth>();

      // co_await tmc::spawn(skynet::coro::bulk::prio_desc::skynet<Depth>(), Depth);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto execDur =
      std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
    std::printf(
      "%" PRIu64 " skynet iterations in %" PRIu64 " us: %" PRIu64
      " thread-us\n",
      IterCount, execDur.count(),
      tmc::cpu_executor().thread_count() * execDur.count()
    );
  }
}

#pragma once
#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"

#include "tmc/ex_cpu.hpp"
#include "tmc/task.hpp"

#include <chrono>
#include <cstdio>

template <size_t Depth = 6> tmc::task<void> loop_skynet() {
  static_assert(Depth <= 6);
  const size_t iter_count = 1000;
  for (size_t j = 0; j < 5; ++j) {
    auto startTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iter_count; ++i) {
      // Different implementations of skynet below. The most efficient
      // implementation, coro::bulk, is the default.

      // co_await skynet::direct::skynet<Depth>();
      // co_await skynet::func::single::skynet<Depth>();
      // co_await skynet::coro::single::skynet<Depth>();

      co_await skynet::coro::bulk::skynet<Depth>();

      // co_await skynet::braids::single::skynet<Depth>();
      // co_await skynet::braids::fork::skynet<Depth>();
      // co_await skynet::braids::bulk::skynet<Depth>();
    }
    auto endTime = std::chrono::high_resolution_clock::now();

    size_t execDur =
      std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime)
        .count();
    std::printf(
      "%zu skynet iterations in %zu us: %zu thread-us\n", iter_count, execDur,
      tmc::cpu_executor().thread_count() * execDur
    );
  }
}

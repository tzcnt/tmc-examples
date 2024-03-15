#pragma once
#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"
#include "tmc/detail/test.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/task.hpp"
#include <cinttypes>
#include <cstdio>

template <size_t Depth = 6> void loop_skynet() {
  static_assert(Depth <= 6);
  const size_t iter_count = 1000;
  for (size_t j = 0; j < 5; ++j) {
    size_t waited_count = 0;
    auto execDur = std::chrono::nanoseconds(0);
    for (size_t i = 0; i < iter_count; ++i) {
      // Different implementations of skynet below. The most efficient
      // implementation, coro::bulk, is the default.

      // co_await skynet::direct::skynet<Depth>();
      // co_await skynet::func::single::skynet<Depth>();
      // co_await skynet::coro::single::skynet<Depth>();
      auto startTime = std::chrono::high_resolution_clock::now();
      tmc::post_waitable(
        tmc::cpu_executor(), skynet::coro::bulk::skynet<Depth>(), 0
      )
        .get();
      auto endTime = std::chrono::high_resolution_clock::now();
      execDur += (endTime - startTime);
      // waited_count +=
      //   tmc::test::wait_for_all_threads_to_sleep(tmc::cpu_executor());

      // co_await skynet::braids::single::skynet<Depth>();
      // co_await skynet::braids::fork::skynet<Depth>();
      // co_await skynet::braids::bulk::skynet<Depth>();
    }
    auto execPrint =
      std::chrono::duration_cast<std::chrono::microseconds>(execDur);
    std::printf(
      "%" PRIu64 " skynet iterations in %" PRIu64 " us: %" PRIu64
      " thread-us\n",
      iter_count, execPrint.count(),
      tmc::cpu_executor().thread_count() *
        static_cast<size_t>(execPrint.count())
    );
    std::printf("waited %" PRIu64 " times\n", waited_count);
  }
}

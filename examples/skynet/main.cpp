// Various implementations of the skynet benchmark as described here:
// https://github.com/atemerev/skynet

// The most efficient implementation is in `skynet::coro::bulk`

// There are also implementations in the priorities/ subfolder,
// which use different priority levels for different depths of the
// task tree. This does not enhance performance.

#define TMC_IMPL

#include "tmc/task.hpp"

#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"
#include "skynet_loop.hpp"

#include "tmc/ex_cpu.hpp"

#include <cstdio>

#define DEPTH 6

int main() {
#ifdef TMC_USE_HWLOC
  // Opt-in to hyperthreading
  tmc::cpu_executor().set_thread_occupancy(2.0f);
#endif
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    co_await loop_skynet<DEPTH>();
    co_return 0;
  }());

  // These each create their own standalone executors
  // tmc::cpu_executor().teardown();
  // skynet::direct::run_skynet<DEPTH>();
  // skynet::func::single::run_skynet<DEPTH>();
  // skynet::coro::single::run_skynet<DEPTH>();
  // skynet::coro::bulk::run_skynet<DEPTH>();
  // skynet::braids::single::run_skynet<DEPTH>();
  // skynet::braids::bulk::run_skynet<DEPTH>();
  // skynet::braids::fork::run_skynet<DEPTH>();
}

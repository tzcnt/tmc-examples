#define TMC_IMPL

#include "priorities/skynet_priorities.hpp"
#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"
#include "skynet_loop.hpp"
#include "tmc/ex_cpu.hpp"

#define DEPTH 6

int main() {
  tmc::cpu_executor().set_priority_count(DEPTH + 1);
  async_main([]() -> tmc::task<int> {
    co_await loop_skynet_prio<DEPTH>();
    co_return 0;
  }());

  // These each create their own standalone executors
  tmc::cpu_executor().teardown();
  skynet::direct::run_skynet<DEPTH>();
  skynet::func::single::run_skynet<DEPTH>();
  skynet::coro::single::run_skynet<DEPTH>();
  skynet::coro::single::prio_asc::run_skynet<DEPTH>();
  skynet::coro::single::prio_desc::run_skynet<DEPTH>();
  skynet::coro::bulk::run_skynet<DEPTH>();
  skynet::coro::bulk::prio_asc::run_skynet<DEPTH>();
  skynet::coro::bulk::prio_desc::run_skynet<DEPTH>();
  skynet::braids::single::run_skynet<DEPTH>();
  skynet::braids::bulk::run_skynet<DEPTH>();
  skynet::braids::fork::run_skynet<DEPTH>();
}

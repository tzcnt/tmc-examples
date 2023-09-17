#define TMC_IMPL
#include "skynet_braid.hpp"
#include "skynet_coro_bulk.hpp"
#include "skynet_coro_single.hpp"
#include "skynet_direct.hpp"
#include "skynet_func.hpp"
#include "skynet_loop.hpp"
#include "tmc/ex_cpu.hpp"
#define DEPTH 6
int main() {
  tmc::cpu_executor().init();
  return async_main([]() -> tmc::task<int> {
    std::printf("sizeof(work_item): %ld\n", sizeof(work_item));
    co_await loop_skynet<DEPTH>();
    co_return 0;
  }());

  // These each create their own standalone executors
  tmc::cpu_executor().teardown();
  skynet::direct::run_skynet<DEPTH>();
  skynet::func::single::run_skynet<DEPTH>();
  skynet::coro::single::run_skynet<DEPTH>();
  skynet::coro::bulk::run_skynet<DEPTH>();
  skynet::braids::single::run_skynet<DEPTH>();
  skynet::braids::bulk::run_skynet<DEPTH>();
  skynet::braids::fork::run_skynet<DEPTH>();
}

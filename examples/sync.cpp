// Demonstrates all the ways you can block synchronously on tasks from external
// code

#define TMC_IMPL

#include "tmc/sync.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/utils.hpp"

#include <cstdio>

using namespace tmc;

tmc::task<void> coro_void(int Slot) {
  std::printf("void coro: %d\n", Slot);
  co_return;
}

tmc::task<int> coro_value(int Slot) {
  std::printf("value coro: %d\n", Slot);
  co_return Slot + 100;
}

void func_void() { std::printf("void func\n"); }

int func_value() {
  std::printf("value func\n");
  return 200;
}

void wait_one_func_void() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), func_void, 0);
  fut.get();
}

void wait_one_func_value() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), func_value, 0);
  auto value = fut.get();
  std::printf("got value %d\n", value);
}

void wait_one_coro_void() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), coro_void(0), 0);
  fut.get();
}

void wait_one_coro_value() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), coro_value(0), 0);
  auto value = fut.get();
  std::printf("got value %d\n", value);
}

void wait_many_func_void() {
  auto fut = tmc::post_bulk_waitable(
    tmc::cpu_executor(), tmc::iter_adapter(0, [](int) { return func_void; }), 0,
    10
  );
  fut.get();
}

void wait_many_coro_void() {
  auto fut = tmc::post_bulk_waitable(
    tmc::cpu_executor(), tmc::iter_adapter(0, coro_void), 0, 10
  );
  fut.get();
}

int main() {
  tmc::cpu_executor().init();
  wait_one_coro_void();
  wait_one_coro_value();
  wait_one_func_void();
  wait_one_func_value();
  wait_many_func_void();
  wait_many_coro_void();
}

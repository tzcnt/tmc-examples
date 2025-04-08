// Demonstrates all the ways you can block synchronously on tasks from external
// code

#define TMC_IMPL

#include "tmc/sync.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/task.hpp"

#include <chrono>
#include <cstdio>
#include <ranges>
#include <thread>

static tmc::task<void> coro_void(int Slot) {
  std::printf("void coro: %d\n", Slot);
  co_return;
}

static tmc::task<int> coro_value(int Slot) {
  std::printf("value coro: %d\n", Slot);
  co_return Slot + 100;
}

static void func_void() { std::printf("void func\n"); }

static int func_value() {
  std::printf("value func\n");
  return 200;
}

static void wait_one_coro_void() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), coro_void(0));
  fut.get();
}

static void wait_one_func_void() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), func_void);
  fut.get();
}

static void wait_one_coro_value() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), coro_value(0));
  auto value = fut.get();
  std::printf("got value %d\n", value);
}

static void wait_one_func_value() {
  auto fut = tmc::post_waitable(tmc::cpu_executor(), func_value);
  auto value = fut.get();
  std::printf("got value %d\n", value);
}

static void wait_many_coro_void() {
  auto fut = tmc::post_bulk_waitable(
    tmc::cpu_executor(),
    (std::ranges::views::iota(0) | std::ranges::views::transform(coro_void))
      .begin(),
    10
  );
  fut.get();
}

static void wait_many_func_void() {
  auto fut = tmc::post_bulk_waitable(
    tmc::cpu_executor(),
    (std::ranges::views::iota(0) |
     std::ranges::views::transform([](int) { return func_void; })
    ).begin(),
    10
  );
  fut.get();
}

static void nowait_one_coro_void() {
  tmc::post(tmc::cpu_executor(), coro_void(0));
}

static void nowait_one_func_void() {
  tmc::post(tmc::cpu_executor(), func_void);
}

static void nowait_many_coro_void() {
  tmc::post_bulk(
    tmc::cpu_executor(),
    (std::ranges::views::iota(0) | std::ranges::views::transform(coro_void))
      .begin(),
    10
  );
}

static void nowait_many_func_void() {
  tmc::post_bulk(
    tmc::cpu_executor(),
    (std::ranges::views::iota(0) |
     std::ranges::views::transform([](int) { return func_void; })
    ).begin(),
    10
  );
}

// The only allowed sync operation on a type that returns a Result (non-void) is
// post_waitable(). You cannot post(), post_bulk(), or post_bulk_waitable()
// these types; therefore, none of the below statements will compile.
static void disallowed_operations() {
  // tmc::post(tmc::cpu_executor(), func_value);

  // tmc::post(tmc::cpu_executor(), coro_value(0));

  // tmc::post_bulk(
  //   tmc::cpu_executor(),
  //   (std::ranges::views::iota(0) | std::ranges::views::transform(coro_value))
  //     .begin(),
  //   10
  // );

  // tmc::post_bulk(
  //   tmc::cpu_executor(),
  //   (std::ranges::views::iota(0) |
  //    std::ranges::views::transform([](int) { return func_value; })
  //   ).begin(),
  //   10
  // );

  // tmc::post_bulk_waitable(
  //   tmc::cpu_executor(),
  //   (std::ranges::views::iota(0) | std::ranges::views::transform(coro_value))
  //     .begin(),
  //   10
  // )
  //   .get();

  // tmc::post_bulk_waitable(
  //   tmc::cpu_executor(),
  //   (std::ranges::views::iota(0) |
  //    std::ranges::views::transform([](int) { return func_value; })
  //   ).begin(),
  //   10
  // )
  //   .get();
}

int main() {
  tmc::cpu_executor().init();
  nowait_one_coro_void();
  nowait_one_func_void();
  nowait_many_coro_void();
  nowait_many_func_void();
  // These are unsynchronized operations, so give them a bit to complete.
  // In a real application, you should do something better...
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  wait_one_coro_void();
  wait_one_func_void();
  wait_one_coro_value();
  wait_one_func_value();
  wait_many_coro_void();
  wait_many_func_void();

  // For exposition only
  disallowed_operations();
}

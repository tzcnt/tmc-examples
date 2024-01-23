#include <coroutine>
#include <iostream>
#include <thread>

#define TMC_IMPL
#include "external_coro.hpp"
#include "tmc/all_headers.hpp"

external_coro<int> external(int Count, int Max);

// A TMC coro that awaits an external coro.
tmc::task<int> internal(int Count, int Max) {
  ++Count;
  if (Count == Max) {
    co_return Count;
  }
  int result = co_await external(Count, Max);
  co_return result;
}

// An external coro that awaits a TMC coro.
external_coro<int> external(int Count, int Max) {
  ++Count;
  if (Count == Max) {
    co_return Count;
  }
  int result = co_await internal(Count, Max);
  co_return result;
}

void run_internal_first(int max) {
  std::future<int> result_future =
    tmc::post_waitable(tmc::cpu_executor(), internal(0, max), 0);
  int result = result_future.get();
  if (result != max) {
    std::printf("wrong result from result_future\n");
  }
}

/// Not currently supported - if you want to get a result from an external
/// coroutine type, you should await it within a TMC coroutine
/// (as in run_internal_first())

// void run_external_first(int max) {
//   std::future<int> result_future =
//     tmc::post_waitable(tmc::cpu_executor(), external(0, max), 0);
//   int result = result_future.get();
//   if (result != max) {
//     std::printf("wrong result from result_future\n");
//   }
// }

int main() {
  tmc::cpu_executor().init();
  run_internal_first(4);
  run_internal_first(5);
}

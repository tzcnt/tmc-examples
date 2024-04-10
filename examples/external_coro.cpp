#define TMC_IMPL

#include "external_coro.hpp"

#include "tmc/ex_cpu.hpp"
#include "tmc/sync.hpp"

#include <cstdio>
#include <future>

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

/// Another option to get a result from an external coroutine type is to
/// manually capture a promise
external_coro<void>
external_result_by_promise(int Count, int Max, std::promise<int>&& promise) {
  ++Count;
  if (Count == Max) {
    promise.set_value(Count);
    co_return;
  }
  int result = co_await internal(Count, Max);
  promise.set_value(result);
  co_return;
}

void run_external_first_by_promise(int max) {
  std::promise<int> result_promise;
  std::future<int> result_future = result_promise.get_future();
  // post_waitable doesn't work, as we have no way to deduce the result type of
  // the external coro. by submitting it with post(), it will simply be treated
  // as a void() functor and invoked (resumed) once.
  tmc::post(
    tmc::cpu_executor(),
    external_result_by_promise(0, max, std::move(result_promise)), 0
  );
  int result = result_future.get();
  if (result != max) {
    std::printf("wrong result from result_future\n");
  }
}

int main() {
  tmc::cpu_executor().init();
  run_internal_first(4);
  run_internal_first(5);
  run_external_first_by_promise(4);
  run_external_first_by_promise(5);
}

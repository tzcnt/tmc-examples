// An implementation of the recursive fork fibonacci parallelism test.
// This is not intended to be an efficient fibonacci calculator,
// but a test of the runtime's fork/join efficiency.

#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <coroutine>
#include <iostream>
#include <thread>

using namespace tmc;

task<size_t> fib(size_t n) {
  if (n < 2)
    co_return n;
  /* Several different ways to spawn / await 2 child tasks */

  /* Execute them one-by-one (serially) */
  // auto x = co_await fib(n - 2);
  // auto y = co_await fib(n - 1);
  // co_return x + y;

  /* Preallocated task array, bulk spawn */
  // std::array<task<size_t>, 2> tasks;
  // tasks[0] = fib(n - 2);
  // tasks[1] = fib(n - 1);
  // auto results = co_await spawn_many<2>(tasks.data(), 0);
  // co_return results[0] + results[1];

  /* Iterator adapter function to generate tasks, bulk spawn */
  // auto results = co_await spawn_many<2>(iter_adapter(n - 2, fib), 0);
  // co_return results[0] + results[1];

  /* Spawn one, then serially execute the other, then await the first */
  auto xt = spawn(fib(n - 1)).run_early();
  auto y = co_await fib(n - 2);
  auto x = co_await xt;
  co_return x + y;
}

task<void> top_fib(size_t n) {
  auto result = co_await fib(n);
  std::printf("%" PRIu64 "\n", result);
  co_return;
}

constexpr size_t NRUNS = 1;
int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Usage: fib <n-th fibonacci number requested>\n");
    exit(0);
  }

  int n = atoi(argv[1]);
  tmc::async_main([](int n) -> tmc::task<int> {
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NRUNS; ++i) {
      co_await top_fib(n), 0;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
      end_time - start_time
    );
    std::printf("%" PRIu64 " us\n", total_time_us.count() / NRUNS);
    co_return 0;
  }(n));
}

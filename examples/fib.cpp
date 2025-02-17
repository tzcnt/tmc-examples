// An implementation of the recursive fork fibonacci parallelism test.
// This is not intended to be an efficient fibonacci calculator,
// but a test of the runtime's fork/join efficiency.

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <chrono>
#include <cstdio>

using namespace tmc;

static task<size_t> fib(size_t n) {
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
  // auto results = co_await spawn_many<2>(tasks.data());
  // co_return results[0] + results[1];

  /* Iterator adapter function to generate tasks, bulk spawn */
  // auto results = co_await spawn_many<2>(iter_adapter(n - 2, fib));
  // co_return results[0] + results[1];

  /* You could also use std::ranges for this */
  /* requires #include <ranges> */
  // auto results = co_await spawn_many<2>((std::ranges::views::iota(n - 2) |
  //                                        std::ranges::views::transform(fib))
  //                                         .begin());
  // co_return results[0] + results[1];

  /* Spawn one, then serially execute the other, then await the first */
  auto xt = spawn(fib(n - 1)).run_early();
  auto y = co_await fib(n - 2);
  auto x = co_await std::move(xt);
  co_return x + y;

  /* Submit using variadic parameter pack, and get the results as a tuple */
  // auto [x, y] = co_await spawn_tuple(fib(n - 1), fib(n - 2));
  // co_return x + y;
}

static task<void> top_fib(size_t n) {
  auto result = co_await fib(n);
  std::printf("%zu\n", result);
  co_return;
}

constexpr size_t NRUNS = 1;
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifndef NDEBUG
  // Hardcode the size in debug mode so we don't have to fuss around with
  // input arguments in the debug config.
  size_t n = 30;
#else
  if (argc != 2) {
    printf("Usage: fib <n-th fibonacci number requested>\n");
    exit(0);
  }

  size_t n = static_cast<size_t>(atoi(argv[1]));
#endif
#ifdef TMC_USE_HWLOC
  // Opt-in to hyperthreading
  tmc::cpu_executor().set_thread_occupancy(2.0f);
#endif
  tmc::async_main([](size_t N) -> tmc::task<int> {
    auto startTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NRUNS; ++i) {
      co_await top_fib(N);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    size_t totalTimeUs =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
        .count();
    std::printf("%zu us\n", totalTimeUs / NRUNS);
    co_return 0;
  }(n));
}

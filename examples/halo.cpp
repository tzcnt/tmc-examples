// An implementation of the recursive fork fibonacci parallelism test.
// This is not intended to be an efficient fibonacci calculator,
// but a test of the runtime's fork/join efficiency.

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

static tmc::task<size_t> fib(size_t n) { co_return n; }

static tmc::task<void> top_fib(size_t n) {
  {
    auto t = co_await tmc::halo_fork(fib(n - 1));
    auto result = co_await std::move(t);
    std::printf("%zu\n", result);
  }

  {
    auto t = co_await tmc::halo_fork_tuple(fib(n - 1));
    auto result = co_await std::move(t);
    std::printf("%zu\n", std::get<0>(result));
  }
  co_return;
}

constexpr size_t NRUNS = 1;
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  size_t n = 2;
#ifdef TMC_USE_HWLOC
  // Opt-in to hyperthreading
  tmc::cpu_executor().set_thread_occupancy(2.0f);
#endif
  tmc::async_main([](size_t N) -> tmc::task<int> {
    auto startTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NRUNS; ++i) {
      co_await tmc::spawn_tuple(top_fib(N));
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    size_t totalTimeUs =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
        .count();
    std::printf("%zu us\n", totalTimeUs / NRUNS);
    co_return 0;
  }(n));
}

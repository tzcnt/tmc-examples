// An implementation of the recursive fork fibonacci parallelism test.
// This is not intended to be an efficient fibonacci calculator,
// but a test of the runtime's fork/join efficiency.

#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/fork_group.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

static tmc::task<size_t> fib(size_t n) {
  // asdf
  co_return n;
}
static tmc::task<void> fibv(size_t n) { co_return; }

static tmc::task<void> top_fib(size_t n) {
  auto fg = tmc::fork_group<3, size_t>();
  // fg.add(fib(1));
  // fg.add(fib(2));
  // fg.add(fib(3));
  // co_await fg.halo_add(fib(1));
  // co_await fg.halo_add(fib(2));
  // co_await fg.halo_add(fib(3));

  // This causes segfault, as all 3 child tasks try to use the same allocation
  // std::array<tmc::task<size_t>, 3> tasks;
  // for (size_t i = 0; i < 3; ++i) {
  //   tasks[i] = co_await fg.halo_add(fib(i));
  // }
  // auto arr = co_await std::move(fg);

  for (size_t i = 0; i < 3; ++i) {
    auto t = co_await tmc::fork_clang(fib(n - 1));
    auto result = co_await std::move(t);
    std::printf("%zu\n", result);
  }

  {
    // auto t = co_await tmc::halo_fork_tuple(fib(n - 1));
    // auto result = co_await std::move(t);
    // std::printf("%zu\n", std::get<0>(result));
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

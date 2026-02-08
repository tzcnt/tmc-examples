// An implementation of the recursive fork fibonacci parallelism test.
// This is not intended to be an efficient fibonacci calculator,
// but a test of the runtime's fork/join efficiency.
// This version links against the TMC DLL rather than compiling TMC inline.

#include "tmc/all_headers.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

static tmc::task<size_t> fib(size_t n) {
  if (n < 2)
    co_return n;

  auto xt = spawn(fib(n - 1)).fork();
  auto y = co_await fib(n - 2);
  auto x = co_await std::move(xt);
  co_return x + y;
}

static tmc::task<void> top_fib(size_t n) {
  auto result = co_await fib(n);
  std::printf("%zu\n", result);
  co_return;
}

constexpr size_t NRUNS = 1;
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifndef NDEBUG
  size_t n = 30;
#else
  if (argc != 2) {
    printf("Usage: fib_dll <n-th fibonacci number requested>\n");
    return -1;
  }

  size_t n = static_cast<size_t>(atoi(argv[1]));
#endif
  tmc::async_main([](size_t N) -> tmc::task<int> {
    auto startTime = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NRUNS; ++i) {
      co_await top_fib(N);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    size_t totalTimeUs = static_cast<size_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
        .count()
    );
    std::printf("%zu us\n", totalTimeUs / NRUNS);
    co_return 0;
  }(n));
}

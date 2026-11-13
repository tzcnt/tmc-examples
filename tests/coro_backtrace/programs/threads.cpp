// Test program for the coroutine backtrace scripts: recursive fork/join on 8
// worker threads. run.py stops in stop_here() after many hits and looks at the
// async stack of every thread (THREADS_* in run.py). See awaits.cpp for the
// anchors.
#include "tmc/all_headers.hpp"

#include <cstddef>
#include <cstdio>
#include <utility>

#ifdef __clang__
[[gnu::noinline]]
#else
[[gnu::noipa]] // see awaits.cpp
#endif
static void stop_here() {
  static volatile int sink;
  sink = sink + 1;
}

static tmc::task<size_t> fib(size_t n) {
  if (n < 2) {
    stop_here();
    co_return n;
  }
  auto xt = tmc::spawn(fib(n - 1)).fork();
  auto y = co_await fib(n - 2); // @fib_plain
  auto x = co_await std::move(xt); // @fib_fork
  co_return x + y;
}

static tmc::task<void> top_fib(size_t n) {
  auto result = co_await fib(n); // @top
  std::printf("%zu\n", result);
}

int main() {
  tmc::cpu_executor().set_thread_count(8);
  return tmc::async_main([](size_t N) -> tmc::task<int> {
    co_await top_fib(N); // @main
    co_return 0;
  }(24));
}

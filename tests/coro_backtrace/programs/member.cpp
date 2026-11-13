// Test program for the coroutine backtrace scripts: coroutines that are member
// functions or lambdas capturing `this`. See awaits.cpp for how the stops and
// the `// @name` anchors work (MEMBER in run.py).
#include "tmc/all_headers.hpp"

#include <cstdio>

// Plain noinline: with GCC -O2 the caller's coroutine frame pointer may stay in
// a caller-saved register across this call. LLDB cannot read it back, GDB can.
[[gnu::noinline]] static void stop_here(int tag) {
  static volatile int sink;
  sink = sink + tag;
}

static tmc::task<int> leaf(int tag) {
  stop_here(tag);
  co_return tag;
}

struct Widget {
  int value = 7;
  tmc::task<int> method(int n) {
    int r = co_await leaf(1); // @site1
    co_return r + value + n;
  }
  tmc::task<int> via_lambda() {
    auto l = [this](int k) -> tmc::task<int> {
      int r = co_await leaf(2); // @site2
      co_return r + value + k;
    };
    int r = co_await l(3); // @site3
    co_return r;
  }
};

int main() {
  tmc::cpu_executor().set_thread_count(1);
  return tmc::async_main([]() -> tmc::task<int> {
    Widget w;
    int a = co_await w.method(1); // @main1
    int b = co_await w.via_lambda(); // @main2
    std::printf("%d\n", a + b);
    co_return 0;
  }());
}

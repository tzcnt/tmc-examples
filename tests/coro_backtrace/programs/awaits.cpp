// Test program for the coroutine backtrace scripts: many shapes of co_await.
//
// Every call to stop_here(tag) is a stop. run.py knows which chain of co_await
// sites leads to each tag (AWAITS in run.py) and reads the line numbers from the
// `// @name` comments in this file. Lines can move freely, but an anchor must
// stay on the line of its co_await. `@name_end` marks the last line of a
// co_await expression that spans several lines.
#include "tmc/all_headers.hpp"

#include <array>
#include <atomic>
#include <coroutine>
#include <cstdio>
#include <exception>
#include <string>
#include <utility>

#ifdef __clang__
[[gnu::noinline]]
#else
// noipa also keeps -fipa-ra from leaving the caller's frame pointer in a
// caller-saved register across this call, which LLDB cannot read back.
// member.cpp does not do this, so that limitation stays covered.
[[gnu::noipa]]
#endif
static void stop_here(int tag) {
  static volatile int sink;
  sink = sink + tag;
}

// For the std::coroutine_handle pretty printer (GDB). Not static, so that they
// survive optimization.
std::coroutine_handle<> g_null_handle;
std::coroutine_handle<> g_live_handle;    // `foreign`, see below
std::coroutine_handle<> g_garbage_handle; // not a coroutine frame
long g_not_a_frame[4] = {1, 2, 3, 4};

static tmc::task<int> leaf(int tag) {
  stop_here(tag);
  co_return tag;
}

// Sequential awaits, loops, branches, a multi-line expression, std::move, two
// awaits on one line.
static tmc::task<int> shapes(int n) {
  bool flagA = true;
  bool flagB = false;
  std::string s = "abc";
  int sum = 0;
  sum += co_await leaf(1); // @site1
  std::string t = std::move(s);
  flagB = flagA;
  sum += co_await leaf(2); // @site2
  for (int i = 0; i < 2; ++i) {
    sum += co_await leaf(3); // @site3
  }
  if (n > 0) {
    sum += co_await leaf(4); // @site4
  } else {
    sum += co_await leaf(5); // @site5
  }
  sum += co_await // @site6
    leaf(
      6
    ); // @site6_end
  auto lt = leaf(7);
  flagA = !flagB;
  sum += co_await std::move(lt); // @site7
  int a = co_await leaf(8); int b = co_await leaf(9); // @site8 @site9
  sum += a + b + static_cast<int>(t.size()) + (flagA ? 1 : 0);
  co_return sum;
}

// Awaits that go through a group: spawn_tuple, spawn_many, fork.
static tmc::task<int> groups() {
  auto [x, y] = co_await tmc::spawn_tuple(leaf(20), leaf(21)); // @site20 @site21
  std::array<tmc::task<int>, 2> arr{leaf(22), leaf(22)};
  auto res = co_await tmc::spawn_many<2>(arr.data()); // @site22
  auto f = tmc::spawn(leaf(23)).fork();
  int z = co_await leaf(24); // @site24
  int w = co_await std::move(f); // @site23
  co_return x + y + res[0] + res[1] + z + w;
}

static tmc::task<int> quiet() { co_return 1; }

#define A1 sum += co_await quiet();
#define A4 A1 A1 A1 A1
#define A16 A4 A4 A4 A4
#define A64 A16 A16 A16 A16
#define A256 A64 A64 A64 A64
// More than 256 suspend points: the suspend index no longer fits in one byte.
static tmc::task<int> big() {
  int sum = 0;
  A256
  A16
  sum += co_await leaf(40); // @site40
  co_return sum;
}

// An await that is resumed by another executor.
static tmc::task<int> braided(tmc::ex_braid& br) {
  int r = co_await tmc::spawn(leaf(50)).run_on(br); // @site50
  co_return r;
}

// A minimal coroutine type that is not a TMC task, awaiting a TMC task. It
// starts eagerly, so it reaches its co_await inside its ramp function.
struct foreign_task {
  struct promise_type {
    long junk0 = 0x1111111111111111;
    long junk1 = 0x2222222222222222;
    long junk2 = 0x3333333333333333;
    foreign_task get_return_object() {
      g_live_handle = std::coroutine_handle<promise_type>::from_promise(*this);
      return {};
    }
    std::suspend_never initial_suspend() noexcept { return {}; }
    std::suspend_never final_suspend() noexcept { return {}; }
    void return_void() {}
    void unhandled_exception() { std::terminate(); }
  };
};
static std::atomic<int> foreign_done{0};
static foreign_task foreign() {
  int r = co_await leaf(30); // @site30
  (void)r;
  foreign_done.store(1);
  foreign_done.notify_all();
}

static tmc::task<int> top() {
  int r = 0;
  r += co_await shapes(1); // @top_shapes
  r += co_await shapes(0); // @top_shapes
  r += co_await groups(); // @top_groups
  r += co_await big(); // @top_big
  tmc::ex_braid br;
  r += co_await braided(br); // @top_braided
  co_return r;
}

int main() {
  g_garbage_handle = std::coroutine_handle<>::from_address(g_not_a_frame);
  tmc::cpu_executor().set_thread_count(1);
  return tmc::async_main([]() -> tmc::task<int> {
    int r = co_await top(); // @main
    foreign();
    foreign_done.wait(0);
    std::printf("%d\n", r);
    co_return 0;
  }());
}

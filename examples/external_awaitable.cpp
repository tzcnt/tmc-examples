// A simple example that shows a tmc::task awaiting an external awaitable.
// Since this external awaitable has no knowledge of TMC, it resumes the task on
// its own thread in the "unsafe" example.

// The "safe" example uses tmc::safe_await_external() to ensure that the TMC
// task is restored to its original executor after the external awaitable
// completes. This method is suitable for use with any unknown awaitable.

// If you want to implement your own awaitables that are aware of TMC executors
// and restore awaiting tasks back to their original executors automatically (so
// that tmc::safe_await_external() is not needed), see the implementation of
// tmc::aw_asio_base::callback::operator() in
// https://github.com/tzcnt/tmc-asio/blob/main/include/tmc/asio/aw_asio.hpp

#define TMC_IMPL

#include "util/thread_name.hpp"

#include "tmc/aw_resume_on.hpp"
#include "tmc/aw_yield.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/sync.hpp"

#include <chrono>
#include <cinttypes>
#include <coroutine>
#include <future>
#include <thread>
#include <utility>

template <typename Result> class external_awaitable {
  Result result;
  std::coroutine_handle<> outer;

public:
  external_awaitable() = default;
  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> Outer) noexcept {
    outer = Outer;
    // Resume the awaiting task on a new thread.
    std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      result = 42;
      outer.resume();
    }).detach();
  }

  Result& await_resume() & noexcept { return result; }
  Result&& await_resume() && noexcept { return std::move(result); }
};

static tmc::task<int> await_external_coro(bool unsafe) {
  std::printf(
    "started on %s at priority %" PRIu64 "\n", get_thread_name().c_str(),
    tmc::current_priority()
  );
  int result;
  if (unsafe) {
    std::printf("co_awaiting external coro (unsafely)...\n");
    result = co_await external_awaitable<int>{};
  } else {
    std::printf("co_awaiting external coro (safely)...\n");
    result = co_await tmc::safe_await_external<int>(external_awaitable<int>{});
  }
  std::printf(
    "resumed on %s at priority %" PRIu64 "\n", get_thread_name().c_str(),
    tmc::current_priority()
  );
  if (result != 42) {
    std::printf("wrong result from external_awaitable\n");
  }
  co_return result;
}

int main() {
  hook_init_ex_cpu_thread_name(tmc::cpu_executor());
  tmc::cpu_executor().set_priority_count(2).init();

  {
    std::future<int> result_future =
      tmc::post_waitable(tmc::cpu_executor(), await_external_coro(true), 1);
    int result = result_future.get();
    if (result != 42) {
      std::printf("wrong result from result_future\n");
    }
  }
  std::printf("\n");

  {
    std::future<int> result_future =
      tmc::post_waitable(tmc::cpu_executor(), await_external_coro(false), 1);
    int result = result_future.get();
    if (result != 42) {
      std::printf("wrong result from result_future\n");
    }
  }
}

// A simple example that shows a tmc::task awaiting an external awaitable.
// Since this external awaitable has no knowledge of TMC, it would resume the
// task on its own thread. However, since tmc::detail::awaitable_traits have not
// been defined for this external awaitable, TMC wraps it into a task that
// restores the original executor and priority.

// If you want to implement your own awaitables that are aware of TMC executors
// and restore awaiting tasks back to their original executors automatically (so
// that this wrapper is not needed), see the implementation of
// tmc::aw_asio_base::callback::operator() in
// https://github.com/tzcnt/tmc-asio/blob/main/include/tmc/asio/aw_asio.hpp
// as well as the specializations of tmc::detail::awaitable_traits in this repo.

#define TMC_IMPL

#include "util/thread_name.hpp"

#include "tmc/current.hpp"
#include "tmc/detail/tiny_lock.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/external.hpp"
#include "tmc/spawn_task.hpp"
#include "tmc/sync.hpp"
#include "tmc/task.hpp"

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdio>
#include <future>
#include <thread>
#include <utility>

template <typename Result> class external_awaitable {
  Result result;
  std::coroutine_handle<> outer;

  tmc::tiny_lock lock;
  std::thread thread;

public:
  external_awaitable() = default;
  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> Outer) noexcept {
    outer = Outer;
    // In the event that the continuation runs immediately, the lock here
    // prevents it from running the destructor until after the thread variable
    // has been populated, and the effects are visible to the resuming thread.
    tmc::tiny_lock_guard lg{lock};
    // Resume the awaiting task on a new thread.
    thread = std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      result = 42;
      outer.resume();
    });
  }

  Result& await_resume() & noexcept { return result; }
  Result&& await_resume() && noexcept { return std::move(result); }

  ~external_awaitable() {
    tmc::tiny_lock_guard lg{lock};
    if (thread.joinable()) {
      thread.join();
    }
  }
};

static tmc::task<int> await_external_coro() {
  std::printf(
    "started on %s at priority %zu\n", get_thread_name().c_str(),
    tmc::current_priority()
  );
  int result;
  std::printf("co_awaiting external awaitable...\n");
  auto exAw = external_awaitable<int>{};
  result = co_await exAw;
  std::printf(
    "resumed on %s at priority %zu\n", get_thread_name().c_str(),
    tmc::current_priority()
  );
  if (result != 42) {
    std::printf("wrong result from external_awaitable\n");
  }
  co_return result;
}

void test_await_external() {
  std::future<int> result_future =
    tmc::post_waitable(tmc::cpu_executor(), await_external_coro(), 1);
  int result = result_future.get();
  if (result != 42) {
    std::printf("wrong result from result_future\n");
  }
  std::printf("\n");
}

void await_external_coro_and_spawn() {}

void test_spawn_on_external_thread() {
  std::printf(
    "running on %s at priority %zu\n", get_thread_name().c_str(),
    tmc::current_priority()
  );
  std::atomic_bool ready(false);
  std::thread thread([&ready]() {
    tmc::spawn([](std::atomic_bool& Ready) -> tmc::task<void> {
      std::printf(
        "child task running on %s at priority %zu\n", get_thread_name().c_str(),
        tmc::current_priority()
      );
      Ready.store(true);
      Ready.notify_all();
      co_return;
    }(ready))
      .detach();
  });
  ready.wait(false);
  thread.join();
}

int main() {
  hook_init_ex_cpu_thread_name(tmc::cpu_executor());
  tmc::cpu_executor().set_priority_count(2).init();

  // This test verifies that, even though the external awaitable tries to hijack
  // the TMC task to its thread, it is automagically returned back to the TMC
  // executor by the safe await machinery of tmc::task::await_transform.
  test_await_external();

  // By configuring the default executor, it will be used in place of the
  // missing thread-local executor on the external thread.
  //
  // This test verifies that when spawn() is called from the external thread,
  // the child task runs on the default executor.
  //
  // If you remove the call to set_default_executor(), the program will simply
  // crash when spawn() is called from the external thread.
  tmc::external::set_default_executor(tmc::cpu_executor());
  test_spawn_on_external_thread();
}

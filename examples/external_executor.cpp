// Create an external executor that exposes all TMC compatibilities.
// For another example, see
// https://github.com/tzcnt/tmc-asio/blob/main/include/tmc/asio/ex_asio.hpp

#define TMC_IMPL

#include "util/thread_name.hpp"

#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task.hpp"
#include "tmc/sync.hpp"

#include <string>
#include <thread>

// A terrible executor that runs everything on a new thread.
// Implements the tmc::detail::TypeErasableExecutor concept.
class external_executor {
  tmc::detail::type_erased_executor type_erased_this;

public:
  external_executor() : type_erased_this(this) {}

  // post() and post_bulk() are the only methods that need to be implemented
  // to construct a type_erased_executor.
  template <typename Functor> void post(Functor&& Func, size_t Priority) {
    std::thread([this, Func] {
      // Thread locals must be setup for each new executor thread
      tmc::detail::this_thread::executor = &type_erased_this; // mandatory
      Func();
    }).detach();
  }

  template <typename FunctorIterator>
  void post_bulk(FunctorIterator FuncIter, size_t Priority, size_t Count) {
    for (size_t i = 0; i < Count; ++i) {
      std::thread([this, Func = *FuncIter] {
        // Thread locals must be setup for each new executor thread
        tmc::detail::this_thread::executor = &type_erased_this; // mandatory
        Func();
      }).detach();
      ++FuncIter;
    }
  }

  // The type_erased() method implements the tmc::detail::TypeErasableExecutor
  // concept. This isn't strictly necessary, but it allows you to pass this
  // directly to certain TMC customization functions, such as run_on() used in
  // this example.
  tmc::detail::type_erased_executor* type_erased() { return &type_erased_this; }
};

external_executor external;

tmc::task<void> child_task() {
  std::printf("child task on %s...\n", get_thread_name().c_str());
  co_return;
}

int main() {
  hook_init_ex_cpu_thread_name(tmc::cpu_executor());
  tmc::cpu_executor().init();

  std::printf("tmc::ex_cpu -> external_executor -> tmc::ex_cpu\n");
  tmc::post_waitable(
    tmc::cpu_executor(),
    []() -> tmc::task<void> {
      std::printf("coro started on %s\n", get_thread_name().c_str());
      std::printf("co_awaiting...\n");
      // run child_task() on the other executor
      co_await tmc::spawn(child_task()).run_on(external);
      std::printf("coro resumed on %s\n", get_thread_name().c_str());
      co_return;
    }(),
    0
  )
    .wait();

  std::printf("\nexternal_executor -> tmc::ex_cpu -> external_executor\n");
  tmc::post_waitable(
    external,
    []() -> tmc::task<void> {
      std::printf("coro started on %s\n", get_thread_name().c_str());
      std::printf("co_awaiting...\n");
      // run child_task() on the other executor
      co_await tmc::spawn(child_task()).run_on(tmc::cpu_executor());
      std::printf("coro resumed on %s\n", get_thread_name().c_str());
      co_return;
    }(),
    0
  )
    .wait();
}
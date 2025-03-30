// Create an external executor that exposes all TMC compatibilities
// by providing a specialization of tmc::detail::executor_traits.
// For another example, see
// https://github.com/tzcnt/tmc-asio/blob/main/include/tmc/asio/ex_asio.hpp

#define TMC_IMPL

#include "tmc/detail/compat.hpp"
#include "tmc/detail/concepts.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task.hpp"
#include "tmc/sync.hpp"
#include "util/thread_name.hpp"

#include <coroutine>
#include <string>
#include <thread>

// A terrible executor that creates a new thread for every task.
class external_executor {
  tmc::detail::ex_any type_erased_this;

public:
  external_executor() : type_erased_this(this) {}

  template <typename Functor>
  void post(
    Functor&& Func, [[maybe_unused]] size_t Priority = 0,
    [[maybe_unused]] size_t ThreadHint = NO_HINT
  ) {
    std::thread([this, func = std::forward<Functor>(Func)] {
      // Thread locals must be setup for each new executor thread
      tmc::detail::this_thread::executor = &type_erased_this; // mandatory
      func();
    }).detach();
  }

  template <typename FunctorIterator>
  void post_bulk(
    FunctorIterator FuncIter, size_t Count,
    [[maybe_unused]] size_t Priority = 0,
    [[maybe_unused]] size_t ThreadHint = NO_HINT
  ) {
    for (size_t i = 0; i < Count; ++i) {
      std::thread([this, func = std::move(*FuncIter)] {
        // Thread locals must be setup for each new executor thread
        tmc::detail::this_thread::executor = &type_erased_this; // mandatory
        func();
      }).detach();
      ++FuncIter;
    }
  }

  tmc::detail::ex_any* type_erased() { return &type_erased_this; }
};

// A complete, minimal implementation of executor_traits.
template <> struct tmc::detail::executor_traits<external_executor> {
  static inline void post(
    external_executor& ex, tmc::work_item&& Item, size_t Priority,
    size_t ThreadHint
  ) {
    ex.post(std::move(Item), Priority, ThreadHint);
  }

  template <typename It>
  static inline void post_bulk(
    external_executor& ex, It&& Items, size_t Count, size_t Priority,
    size_t ThreadHint
  ) {
    ex.post_bulk(std::forward<It>(Items), Count, Priority, ThreadHint);
  }

  static inline tmc::detail::ex_any* type_erased(external_executor& ex) {
    return ex.type_erased();
  }

  static inline std::coroutine_handle<> task_enter_context(
    external_executor& ex, std::coroutine_handle<> Outer, size_t Priority
  ) {
    ex.post(Outer, Priority);
    return std::noop_coroutine();
  }
};

static external_executor external{};

static tmc::task<void> child_task() {
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
    }()
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
    }()
  )
    .wait();
}

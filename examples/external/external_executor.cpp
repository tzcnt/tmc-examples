// Demonstrates how to integrate a completely external (3rd party) library's
// thread pool and turn it into a TMC executor.
//
// external_executor.hpp contains the "3rd party" thread pool. It has no
// knowledge of TMC. Below we build a thin adapter (tmc_external_executor) that:
//   1. Hosts a tmc::ex_any so TMC can identify this executor and route work to
//      it in a type-erased manner.
//   2. Owns a worker thread that participates in the pool's run() loop. Before
//      entering run(), the worker installs the TMC thread-local executor
//      pointer so that TMC coroutines resumed on this thread know where they
//      are running.
//   3. Bridges TMC's priority-aware post() onto the pool's priority-agnostic
//      post(), by capturing the requested priority and applying it to the TMC
//      thread-local before each work item runs.
//
// For another example, see
// https://github.com/tzcnt/TooManyCooks/blob/main/include/tmc/asio/ex_asio.hpp

#include "external_executor.hpp"

#include "../util/thread_name.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/detail/concepts_awaitable.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/ex_any.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn.hpp"
#include "tmc/sync.hpp"
#include "tmc/task.hpp"
#include "tmc/work_item.hpp"

#include <coroutine>
#include <cstddef>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>

// Adapts a 3rd party external_executor to the TMC executor interface.
class tmc_external_executor {
  external_executor pool;
  tmc::ex_any type_erased_this;
  std::thread worker;

public:
  tmc_external_executor() : type_erased_this(this) {
    worker = std::thread([this] {
      // Thread locals must be set up for each executor thread. This tells TMC
      // which executor is currently running, and must be done before run().
      tmc::detail::this_thread::executor() = &type_erased_this; // mandatory
      this_thread::thread_name = "external thread";             // only for this example
      pool.run();
    });
  }

  ~tmc_external_executor() {
    pool.request_stop();
    worker.join();
  }

  void post(
    tmc::work_item&& Item, size_t Priority = 0,
    [[maybe_unused]] size_t ThreadHint = NO_HINT
  ) {
    // The pool has no notion of priority, so we wrap the work item in a functor
    // that installs the requested priority into the TMC thread-local before
    // running it. This ensures awaitables observe the correct priority.
    pool.post([Priority, item = std::move(Item)]() {
      tmc::detail::this_thread::this_task().prio = Priority;
      item();
    });
  }

  // ex_any type-erasure requires a post_bulk() as well; this executor simply
  // forwards each item to post() one at a time.
  template <typename Iter>
  void
  post_bulk(Iter It, size_t Count, size_t Priority = 0, size_t ThreadHint = NO_HINT) {
    for (size_t i = 0; i < Count; ++i) {
      post(std::move(*It), Priority, ThreadHint);
      ++It;
    }
  }

  /// Returns a pointer to the type erased `ex_any` version of this executor.
  /// This object shares a lifetime with this executor, and can be used for
  /// pointer-based equality comparison against
  /// the thread-local `tmc::current_executor()`.
  tmc::ex_any* type_erased() { return &type_erased_this; }
};

// A complete, minimal implementation of executor_traits.
template <> struct tmc::detail::executor_traits<tmc_external_executor> {
  static inline void post(
    tmc_external_executor& Ex, tmc::work_item&& Item, size_t Priority, size_t ThreadHint
  ) {
    Ex.post(std::move(Item), Priority, ThreadHint);
  }

  template <typename It>
  static inline void post_bulk(
    tmc_external_executor& Ex, It&& Items, size_t Count, size_t Priority,
    size_t ThreadHint
  ) {
    Ex.post_bulk(std::forward<It>(Items), Count, Priority, ThreadHint);
  }

  static inline tmc::ex_any* type_erased(tmc_external_executor& Ex) {
    return Ex.type_erased();
  }

  static inline std::coroutine_handle<>
  dispatch(tmc_external_executor& Ex, std::coroutine_handle<> Outer, size_t Priority) {
    Ex.post(std::move(Outer), Priority);
    return std::noop_coroutine();
  }
};

static tmc::task<void> child_task() {
  std::printf("child task on %s...\n", get_thread_name().c_str());
  co_return;
}

int main() {
  hook_init_ex_cpu_thread_name(tmc::cpu_executor());
  tmc::cpu_executor().init();

  tmc_external_executor external{};

  std::printf("tmc::ex_cpu -> external_executor -> tmc::ex_cpu\n");
  tmc::post_waitable(
    tmc::cpu_executor(),
    [&]() -> tmc::task<void> {
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

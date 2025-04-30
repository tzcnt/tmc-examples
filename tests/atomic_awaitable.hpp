// Helper to allow awaiting for an atomic counter to reach a certain value.
// Creates a helper thread for each await operation so as to not block the
// executor threads.

#pragma once

#include "tmc/current.hpp"
#include "tmc/detail/awaitable_customizer.hpp"
#include "tmc/detail/concepts_awaitable.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/detail/tiny_lock.hpp"

#include <atomic>
#include <cassert>
#include <coroutine>
#include <thread>

struct AtomicAwaitableTag {};

// A one-shot awaitable that creates a thread to (blocking) wait until the
// atomic variable reaches the desired variable.
template <typename T> struct atomic_awaitable : private AtomicAwaitableTag {
  std::atomic<T> value;
  T until;
  tmc::detail::awaitable_customizer<void> customizer;
  size_t prio;
  std::thread thread;
  tmc::tiny_lock lock;

  atomic_awaitable(T Until) : value(0), until(Until) {
    if (tmc::detail::this_thread::executor == nullptr) {
      customizer.flags = 0;
    }
  }

  void inc() {
    ++value;
    value.notify_all();
  }

  std::atomic<T>& ref() { return value; }
  operator std::atomic<T>&() { return value; }
  T load() { return value.load(); }

  void async_initiate() {
    // In the event that the continuation runs immediately, the lock here
    // prevents it from running the destructor until after the thread variable
    // has been populated, and the effects are visible to the resuming thread.
    tmc::tiny_lock_guard lg{lock};
    thread = std::thread([this]() {
      auto old = value.load();
      while (old != until) {
        value.wait(old);
        old = value.load();
      }
      auto next = customizer.resume_continuation();
      assert(next == std::noop_coroutine());
    });
  }

  bool await_ready() { return value.load() == until; }

  TMC_FORCE_INLINE inline void await_suspend(std::coroutine_handle<> Outer
  ) noexcept {
    customizer.continuation = Outer.address();
    async_initiate();
  }

  void await_resume() noexcept {}

  ~atomic_awaitable() {
    tmc::tiny_lock_guard lg{lock};
    if (thread.joinable()) {
      thread.join();
    }
  }
};

namespace tmc::detail {

template <typename T>
concept IsAwAtomic = std::is_base_of_v<AtomicAwaitableTag, T>;

template <IsAwAtomic Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = void;
  using self_type = Awaitable;

  // Values controlling the behavior when awaited directly in a tmc::task
  static decltype(auto) get_awaiter(self_type& awaitable) { return awaitable; }

  // Values controlling the behavior when wrapped by a utility function
  // such as tmc::spawn_*()
  static constexpr configure_mode mode = ASYNC_INITIATE;
  static void async_initiate(
    self_type&& awaitable, [[maybe_unused]] tmc::ex_any* Executor,
    [[maybe_unused]] size_t Priority
  ) {
    awaitable.async_initiate();
  }

  static void set_continuation(self_type& awaitable, void* Continuation) {
    awaitable.customizer.continuation = Continuation;
  }

  static void set_continuation_executor(self_type& awaitable, void* ContExec) {
    awaitable.customizer.continuation_executor = ContExec;
  }

  static void set_done_count(self_type& awaitable, void* DoneCount) {
    awaitable.customizer.done_count = DoneCount;
  }

  static void set_flags(self_type& awaitable, size_t Flags) {
    awaitable.customizer.flags = Flags;
  }
};
} // namespace tmc::detail

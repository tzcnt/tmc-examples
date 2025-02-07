// Helper to allow awaiting for an atomic counter to reach a certain value.
// Creates a helper thread for each await operation so as to not block the
// executor threads.

#include "tmc/detail/concepts.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/task.hpp"

#include <atomic>
#include <coroutine>
#include <thread>
#include <utility>
struct AtomicAwaitableTag {};

template <typename T> struct atomic_awaitable : private AtomicAwaitableTag {
  std::atomic<T> value;
  T until;
  tmc::detail::awaitable_customizer<void> customizer;
  size_t prio;

  atomic_awaitable(T begin, T wait_until) : value(begin), until(wait_until) {
    if (tmc::detail::this_thread::executor != nullptr) {
      prio = tmc::detail::this_thread::this_task.prio;
    } else {
      prio = 0;
    }
  }

  std::atomic<T>& ref() { return value; }
  operator std::atomic<T>&() { return value; }
  T load() { return value.load(); }

  void async_initiate() {
    std::thread([this]() {
      auto old = value.load();
      while (old != until) {
        value.wait(old);
        old = value.load();
      }
      auto next = customizer.resume_continuation(prio);
      assert(next == std::noop_coroutine());
    }).detach();
  }

  bool await_ready() { return value.load() == until; }

  TMC_FORCE_INLINE inline void await_suspend(std::coroutine_handle<> Outer
  ) noexcept {
    customizer.continuation = Outer.address();
    async_initiate();
  }

  void await_resume() noexcept {}
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
    self_type&& awaitable,
    [[maybe_unused]] tmc::detail::type_erased_executor* Executor,
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

  static void set_flags(self_type& awaitable, uint64_t Flags) {
    awaitable.customizer.flags = Flags;
  }
};
} // namespace tmc::detail

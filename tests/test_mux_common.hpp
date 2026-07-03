// Shared awaitable helper types for the mux_tuple and mux_many tests. These
// exercise every runtime mode of tmc::detail::awaitable_traits<T>::mode that
// fork() can be handed (TMC_TASK and COROUTINE via tmc::task and
// mux_coroutine_op, ASYNC_INITIATE - both lvalue- and rvalue-qualified - via
// atomic_awaitable and mux_rvalue_async_op, and WRAPPER via mux_wrapper_int).
// UNKNOWN is the not-an-awaitable case and is rejected by a static_assert inside
// fork(), so it is not runtime-testable.

#pragma once

#include "test_common.hpp"

#include <atomic>
#include <cassert>
#include <coroutine>
#include <thread>
#include <type_traits>

// A WRAPPER-mode awaitable: it implements the await_ready/await_suspend/
// await_resume interface directly and has no awaitable_traits specialization, so
// the mux must route it through safe_wrap(). Immediately ready; yields a stored
// int. Its await_* are const, so it is re-awaitable and works whether borrowed
// as an lvalue or moved as an rvalue.
struct mux_wrapper_int {
  int v;
  bool await_ready() const noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  int await_resume() const noexcept { return v; }
};

// A COROUTINE-mode awaitable: it wraps a tmc::task and is convertible to a
// std::coroutine_handle<>, so the mux resumes it as a raw coroutine (the
// non-ASYNC_INITIATE branch) the same way it would a TMC_TASK - but with
// configure_mode COROUTINE rather than TMC_TASK. All customization is forwarded
// to the wrapped task's promise, so it behaves like the task it carries.
struct MuxCoroutineOpTag {};
template <typename Result> struct mux_coroutine_op : private MuxCoroutineOpTag {
  using result_type = Result;
  tmc::task<Result> inner;

  explicit mux_coroutine_op(tmc::task<Result> Inner) : inner(std::move(Inner)) {}

  operator std::coroutine_handle<>() && noexcept {
    return std::coroutine_handle<>(static_cast<tmc::task<Result>&&>(inner));
  }
};

namespace tmc::detail {
template <typename T>
concept IsMuxCoroutineOp = std::is_base_of_v<MuxCoroutineOpTag, T>;

template <IsMuxCoroutineOp Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = typename Awaitable::result_type;
  using self_type = Awaitable;

  static constexpr configure_mode mode = COROUTINE;

  static void
  set_result_ptr(self_type& a, tmc::detail::result_storage_t<result_type>* ResultPtr) {
    a.inner.promise().customizer.result_ptr = ResultPtr;
  }
  static void set_continuation(self_type& a, void* Continuation) {
    a.inner.promise().customizer.continuation = Continuation;
  }
  static void set_continuation_executor(self_type& a, void* ContExec) {
    a.inner.promise().customizer.continuation_executor = ContExec;
  }
  static void set_done_count(self_type& a, void* DoneCount) {
    a.inner.promise().customizer.done_count = DoneCount;
  }
  static void set_flags(self_type& a, size_t Flags) {
    a.inner.promise().customizer.flags = Flags;
  }
};
} // namespace tmc::detail

// An rvalue-qualified (consume-once), non-movable, void-result ASYNC_INITIATE
// awaitable: its trait's async_initiate takes self_type&&. It is non-movable and
// reads `this`, so even when passed as an rvalue it is borrowed (not moved) by
// fork() and remains valid to drive in place. Completion is driven by
// complete(). Mirrors the rvalue_cancellable_op used by the select() tests.
struct MuxRvalueAsyncOpTag {};

struct mux_rvalue_async_op : private MuxRvalueAsyncOpTag {
  std::atomic<int> signalled;
  tmc::detail::awaitable_customizer<void> customizer;
  std::thread thread;
  tmc::tiny_lock lock;

  mux_rvalue_async_op() : signalled(0) {
    if (tmc::current_executor() == nullptr) {
      customizer.flags = 0;
    }
  }

  void complete() {
    signalled.store(1);
    signalled.notify_all();
  }

  void async_initiate() {
    // The lock prevents the destructor from running (and joining an empty
    // thread) before `thread` is populated, in case the continuation runs
    // immediately on another thread.
    tmc::tiny_lock_guard lg{lock};
    thread = std::thread([this]() {
      int old = signalled.load();
      while (old == 0) {
        signalled.wait(old);
        old = signalled.load();
      }
      [[maybe_unused]] auto next = customizer.resume_continuation();
      assert(next == std::noop_coroutine());
    });
  }

  ~mux_rvalue_async_op() {
    tmc::tiny_lock_guard lg{lock};
    if (thread.joinable()) {
      thread.join();
    }
  }
};

// A movable, non-default-constructible, int-result ASYNC_INITIATE awaitable
// that completes synchronously inside async_initiate(): the result is written
// and the continuation resumed before async_initiate() returns. Because it is
// not default-constructible, it cannot be buffered in a std::array, so eager
// construction from an iterator of these must initiate them one at a time.
struct MuxImmediateAsyncOpTag {};

struct mux_immediate_async_op : private MuxImmediateAsyncOpTag {
  int value;
  tmc::detail::awaitable_customizer<int> customizer;

  explicit mux_immediate_async_op(int Value) : value(Value) {}
};

namespace tmc::detail {
template <typename T>
concept IsMuxImmediateAsyncOp = std::is_base_of_v<MuxImmediateAsyncOpTag, T>;

template <IsMuxImmediateAsyncOp Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = int;
  using self_type = Awaitable;

  static decltype(auto) get_awaiter(self_type& awaitable) noexcept { return awaitable; }

  static constexpr configure_mode mode = ASYNC_INITIATE;

  static void async_initiate(
    self_type&& awaitable, [[maybe_unused]] tmc::ex_any* Executor,
    [[maybe_unused]] size_t Priority
  ) {
    *awaitable.customizer.result_ptr = awaitable.value;
    // The owner cannot be suspended while it is initiating awaitables, so this
    // never resumes the continuation inline; it just marks the slot ready.
    [[maybe_unused]] auto next = awaitable.customizer.resume_continuation();
    assert(next == std::noop_coroutine());
  }

  static void
  set_result_ptr(self_type& awaitable, tmc::detail::result_storage_t<int>* ResultPtr) {
    awaitable.customizer.result_ptr = ResultPtr;
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

namespace tmc::detail {
template <typename T>
concept IsMuxRvalueAsyncOp = std::is_base_of_v<MuxRvalueAsyncOpTag, T>;

template <IsMuxRvalueAsyncOp Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = void;
  using self_type = Awaitable;

  static decltype(auto) get_awaiter(self_type& awaitable) noexcept { return awaitable; }

  static constexpr configure_mode mode = ASYNC_INITIATE;
  // rvalue-qualified: this awaitable is consume-once (passed as an rvalue).
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

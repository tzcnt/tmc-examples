#include "test_common.hpp"
#include "test_spawn_many_common.hpp"

#include <gtest/gtest.h>

// tests ported from examples/spawn_iterator.cpp

TEST_F(CATEGORY, spawn_tuple_task_detach) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> counter(2);

    auto ts = tmc::spawn_tuple(
      [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        Counter.notify_all();
        co_return;
      }(counter),
      [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        Counter.notify_all();
        co_return;
      }(counter)
    );
    ts.detach();
    co_await counter;
    EXPECT_EQ(counter.load(), 2);
  }());
}

TEST_F(CATEGORY, spawn_tuple_empty) {
  test_async_main(ex(), []() -> tmc::task<void> {
    [[maybe_unused]] std::tuple<> results = co_await tmc::spawn_tuple();
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_func) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::tuple<int, int, int> results =
      co_await tmc::spawn_tuple(work(0), work(1), work(2));

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_lambda) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      // non-capturing lambda coroutine
      auto f = [](int i) -> tmc::task<int> { co_return 1 << i; };
      std::tuple<int, int, int> results = co_await tmc::spawn_tuple(f(0), f(1), f(2));

      [[maybe_unused]] auto sum =
        std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

      EXPECT_EQ(sum, (1 << 3) - 1);
    }
    {
      // capturing lambda that forwards to non-capturing lambda coroutine
      int i = 0;
      auto f = [&i]() -> tmc::task<int> {
        return [](int j) -> tmc::task<int> { co_return 1 << j; }(i++);
      };
      std::tuple<int, int, int> results = co_await tmc::spawn_tuple(f(), f(), f());

      [[maybe_unused]] auto sum =
        std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

      EXPECT_EQ(sum, (1 << 3) - 1);
    }
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_tuple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::tuple<tmc::task<int>, tmc::task<int>, tmc::task<int>> tasks{
      work(0), work(1), work(2)
    };

    std::tuple<int, int, int> results = co_await tmc::spawn_tuple(std::move(tasks));

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_fork) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto ts = tmc::spawn_tuple(work(0), work(1), work(2));
    auto early = std::move(ts).fork();

    std::tuple<int, int, int> results = co_await std::move(early);

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

TEST_F(CATEGORY, spawn_tuple_from_tuple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::tuple<tmc::task<int>, tmc::task<int>> t{work(0), work(1)};

    std::tuple<int, int> results = co_await tmc::spawn_tuple(std::move(t));

    [[maybe_unused]] auto sum = std::get<0>(results) + std::get<1>(results);

    EXPECT_EQ(sum, (1 << 2) - 1);
  }());
}

// aw_fork_tuple_clang (the dummy awaitable returned by fork_tuple_clang()) has an
// await_ready() that returns true, so a normal co_await goes straight to
// await_resume() and its await_suspend() is never reached. Exercise the awaiter
// interface directly to cover and document that no-op suspend, then join the
// fork so the forked tasks are consumed.
TEST_F(CATEGORY, fork_tuple_clang_await_suspend_noop) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto dummy = tmc::fork_tuple_clang(
      []() -> tmc::task<int> { co_return 3; }(), []() -> tmc::task<int> { co_return 4; }()
    );
    EXPECT_TRUE(dummy.await_ready());
    dummy.await_suspend(std::noop_coroutine()); // no-op; never reached by co_await
    auto forked = dummy.await_resume();
    std::tuple<int, int> results = co_await std::move(forked);
    EXPECT_EQ(std::get<0>(results), 3);
    EXPECT_EQ(std::get<1>(results), 4);
  }());
}

/*** select() tests ***/

// A manufactured awaitable that is also its own cancellation handle: it can be
// co_awaited (ASYNC_INITIATE) and exposes a .cancel() method, to exercise the
// single-argument tmc::cancellable() overload. It completes only when signalled
// - either by cancel() (invoked by select() on a loser) or by complete() (used
// by tests to make it win). It is awaited as an lvalue, so its trait's
// async_initiate takes self_type&.
struct CancellableOpTag {};

struct cancellable_op : private CancellableOpTag {
  std::atomic<int> signalled;
  tmc::detail::awaitable_customizer<void> customizer;
  std::thread thread;
  tmc::tiny_lock lock;

  cancellable_op() : signalled(0) {
    if (tmc::current_executor() == nullptr) {
      customizer.flags = 0;
    }
  }

  void signal() {
    signalled.store(1);
    signalled.notify_all();
  }
  void cancel() { signal(); }   // invoked by select() to cancel this as a loser
  void complete() { signal(); } // used by tests to make this win the select

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

  ~cancellable_op() {
    tmc::tiny_lock_guard lg{lock};
    if (thread.joinable()) {
      thread.join();
    }
  }
};

namespace tmc::detail {
template <typename T>
concept IsCancellableOp = std::is_base_of_v<CancellableOpTag, T>;

template <IsCancellableOp Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = void;
  using self_type = Awaitable;

  static decltype(auto) get_awaiter(self_type& awaitable) noexcept { return awaitable; }

  static constexpr configure_mode mode = ASYNC_INITIATE;
  static void async_initiate(
    self_type& awaitable, [[maybe_unused]] tmc::ex_any* Executor,
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

// Like cancellable_op, but rvalue-qualified: its trait's async_initiate takes
// self_type&& (a consume-once awaitable). It is non-movable and reads `this`, so
// even when awaited as an rvalue it is borrowed (not moved) by spawn_tuple and
// remains valid to cancel in place. Used to confirm select() forwards an
// awaitable's original value category rather than forcing lvalue.
struct RvalueCancellableOpTag {};

struct rvalue_cancellable_op : private RvalueCancellableOpTag {
  std::atomic<int> signalled;
  tmc::detail::awaitable_customizer<void> customizer;
  std::thread thread;
  tmc::tiny_lock lock;

  rvalue_cancellable_op() : signalled(0) {
    if (tmc::current_executor() == nullptr) {
      customizer.flags = 0;
    }
  }

  void cancel() {
    signalled.store(1);
    signalled.notify_all();
  }
  void complete() { cancel(); }

  void async_initiate() {
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

  ~rvalue_cancellable_op() {
    tmc::tiny_lock_guard lg{lock};
    if (thread.joinable()) {
      thread.join();
    }
  }
};

namespace tmc::detail {
template <typename T>
concept IsRvalueCancellableOp = std::is_base_of_v<RvalueCancellableOpTag, T>;

template <IsRvalueCancellableOp Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = void;
  using self_type = Awaitable;

  static decltype(auto) get_awaiter(self_type& awaitable) noexcept { return awaitable; }

  static constexpr configure_mode mode = ASYNC_INITIATE;
  // rvalue-qualified: this awaitable is consume-once (awaited as an rvalue).
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

// Like cancellable_op, but its .cancel() is *asynchronous*: it returns a
// tmc::task<void> that performs the cancellation when awaited. Exercises the
// single-argument overload's awaitable-cancel path - select() co_awaits the
// returned task before draining. Awaited as an lvalue, like cancellable_op.
struct AsyncCancellableOpTag {};

struct async_cancellable_op : private AsyncCancellableOpTag {
  std::atomic<int> signalled;
  tmc::detail::awaitable_customizer<void> customizer;
  std::thread thread;
  tmc::tiny_lock lock;

  async_cancellable_op() : signalled(0) {
    if (tmc::current_executor() == nullptr) {
      customizer.flags = 0;
    }
  }

  void signal() {
    signalled.store(1);
    signalled.notify_all();
  }
  // Asynchronous cancel: the work happens when select() co_awaits this task.
  tmc::task<void> cancel() {
    signal();
    co_return;
  }
  void complete() { signal(); } // used by tests to make this win the select

  void async_initiate() {
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

  ~async_cancellable_op() {
    tmc::tiny_lock_guard lg{lock};
    if (thread.joinable()) {
      thread.join();
    }
  }
};

namespace tmc::detail {
template <typename T>
concept IsAsyncCancellableOp = std::is_base_of_v<AsyncCancellableOpTag, T>;

template <IsAsyncCancellableOp Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = void;
  using self_type = Awaitable;

  static decltype(auto) get_awaiter(self_type& awaitable) noexcept { return awaitable; }

  static constexpr configure_mode mode = ASYNC_INITIATE;
  static void async_initiate(
    self_type& awaitable, [[maybe_unused]] tmc::ex_any* Executor,
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

// The cancel_self_t marker must occupy no storage: a self-cancel pair is the
// size of the (owned) awaitable alone. Checked with a movable type to confirm
// the value-storage (owning) path also pays no marker overhead.
static_assert(
  sizeof(tmc::cancellable<std::unique_ptr<int>, tmc::detail::cancel_self_t>) ==
    sizeof(std::unique_ptr<int>),
  "cancel_self_t should occupy no storage in tmc::cancellable"
);

// Minimal awaitable that is also its own cancel handle and is *movable*. Never
// actually awaited - it exists only for the compile-time checks below, so its
// trait declares just enough (result_type) to satisfy is_awaitable.
struct MovableSelfCancelOpTag {};
struct movable_self_cancel_op : private MovableSelfCancelOpTag {
  void cancel() {}
};
namespace tmc::detail {
template <typename T>
concept IsMovableSelfCancelOp = std::is_base_of_v<MovableSelfCancelOpTag, T>;
template <IsMovableSelfCancelOp Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = void;
};
} // namespace tmc::detail

// Detects whether the single-argument tmc::cancellable() overload accepts an
// input of value category T (`std::declval<T>()` yields an lvalue for `U&`, an
// rvalue otherwise). Routing the check through a T-dependent concept makes a
// no-viable-overload a substitution failure in the immediate context (a clean
// `false`) rather than a hard error.
template <typename T>
concept SelfCancellableInput = requires { tmc::cancellable(std::declval<T>()); };

// The single-argument tmc::cancellable() overload (self-cancel) accepts an
// awaitable only when the pair can *borrow* it - an lvalue or a non-movable
// rvalue. A movable rvalue would be owned by value, migrate into spawn_tuple,
// and leave cancellation targeting a moved-from husk, so it is rejected at
// compile time. These checks lock that value-category contract in; note the
// rejection is by value category, not by type (a movable type by lvalue is
// accepted).
static_assert(
  std::is_move_constructible_v<movable_self_cancel_op>,
  "test precondition: movable_self_cancel_op must be movable"
);
static_assert(
  SelfCancellableInput<cancellable_op&>,
  "self-cancel cancellable() should accept an lvalue"
);
static_assert(
  SelfCancellableInput<rvalue_cancellable_op>,
  "self-cancel cancellable() should accept a non-movable rvalue"
);
static_assert(
  !SelfCancellableInput<movable_self_cancel_op>,
  "self-cancel cancellable() must reject a movable rvalue"
);
static_assert(
  SelfCancellableInput<movable_self_cancel_op&>,
  "self-cancel cancellable() should accept a movable type passed by lvalue"
);

// The winner (index 0) completes immediately; the loser at index 1 blocks on
// an event until its canceller signals it, then is drained. Exercises the
// void -> std::monostate result mapping.
TEST_F(CATEGORY, select_first_wins) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<void> { co_return; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(loser(block), [&] { block.inc(); })
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

// The winner is at index 1, so the leading awaitable (index 0) is the one that
// gets cancelled.
TEST_F(CATEGORY, select_second_wins) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<void> { co_return; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(loser(block), [&] { block.inc(); }),
      tmc::cancellable(winner(), [] {})
    );

    EXPECT_EQ(result.index(), 1u);
  }());
}

// The winner's value is propagated into the variant. The loser blocks on an
// event until its canceller signals it, then is drained.
TEST_F(CATEGORY, select_returns_winner_value) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<int> { co_return 100; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 200;
    };

    auto result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(loser(block), [&] { block.inc(); })
    );

    EXPECT_EQ(result.index(), 0u);
    EXPECT_EQ(std::get<0>(result), 100);
  }());
}

// The object-reference overload of cancellable() synthesizes a canceller that
// calls the object's .cancel() method.
TEST_F(CATEGORY, select_cancellable_object) {
  test_async_main(ex(), []() -> tmc::task<void> {
    struct event_canceller {
      atomic_awaitable<int>& block;
      void cancel() { block.inc(); }
    };
    atomic_awaitable<int> block(1);
    event_canceller stopper{block};

    auto winner = []() -> tmc::task<int> { co_return 5; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 6;
    };

    auto result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(loser(block), stopper) // object-reference overload
    );

    EXPECT_EQ(result.index(), 0u);
    EXPECT_EQ(std::get<0>(result), 5);
  }());
}

// Exercises the optional-unwrap path for non-default-constructible results.
TEST_F(CATEGORY, select_non_default_constructible_result) {
  struct NoDefault {
    int v;
    NoDefault() = delete;
    explicit NoDefault(int V) : v(V) {}
  };
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<NoDefault> { co_return NoDefault{7}; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<NoDefault> {
      co_await B;
      co_return NoDefault{9};
    };

    auto result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(loser(block), [&] { block.inc(); })
    );

    EXPECT_EQ(result.index(), 0u);
    EXPECT_EQ(std::get<0>(result).v, 7);
  }());
}

// The single-argument cancellable() overload: the object is both the awaitable
// and its own cancellation handle. Here it loses - a task completes first, and
// select() cancels the op in place via its .cancel() method, then drains it.
TEST_F(CATEGORY, select_self_cancellable_loses) {
  test_async_main(ex(), []() -> tmc::task<void> {
    cancellable_op op;

    auto winner = []() -> tmc::task<void> { co_return; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(op) // single-argument overload
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

// Same single-argument overload, but now the self-cancellable op wins: it is
// pre-completed, and the blocking loser is cancelled by its lambda canceller.
TEST_F(CATEGORY, select_self_cancellable_wins) {
  test_async_main(ex(), []() -> tmc::task<void> {
    cancellable_op op;
    op.complete(); // pre-complete so this wins the select

    atomic_awaitable<int> block(1);
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(op), // single-argument overload
      tmc::cancellable(loser(block), [&] { block.inc(); })
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

// The two-argument object overload forwards the cancel-target's value category:
// a movable rvalue object is moved into (owned by) the canceller, so a temporary
// cancellation handle stays alive for the duration of the select.
TEST_F(CATEGORY, select_cancellable_object_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Move-constructible (holds a reference), so it is owned by value.
    struct event_canceller {
      atomic_awaitable<int>& block;
      void cancel() { block.inc(); }
    };
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<int> { co_return 5; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 6;
    };

    auto result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(loser(block), event_canceller{block}) // rvalue -> owned
    );

    EXPECT_EQ(result.index(), 0u);
    EXPECT_EQ(std::get<0>(result), 5);
  }());
}

// The single-argument overload forwards the awaitable's original value category.
// A non-movable, rvalue-qualified (consume-once) awaitable is awaited as an
// rvalue but still borrowed (it can't be moved), so it can be cancelled in place.
// This confirms select() no longer forces self-cancel awaitables to be
// lvalue-qualified.
TEST_F(CATEGORY, select_self_cancellable_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto winner = []() -> tmc::task<void> { co_return; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(rvalue_cancellable_op{}) // rvalue-qualified, non-movable
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

// Overload 1 with a plain (non-coroutine) callable that *returns* a task:
// select() invokes the canceller, then co_awaits the task it returns (an
// asynchronous cancel). The task releases the blocked loser, which is drained.
TEST_F(CATEGORY, select_task_canceller) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<void> { co_return; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };
    auto cancelTask = [](atomic_awaitable<int>& B) -> tmc::task<void> {
      B.inc();
      co_return;
    };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      // callable returning a task; the task is built only when select() cancels
      tmc::cancellable(loser(block), [&] { return cancelTask(block); })
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

// The canceller of the *winner* is never invoked. With a callable that returns a
// task, that means the task is never even built (no coroutine frame is created),
// so there is nothing to leak. `neverRun` fails the test if it is ever called.
TEST_F(CATEGORY, select_winner_canceller_not_invoked) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<void> { co_return; };
    auto neverRun = []() -> tmc::task<void> {
      ADD_FAILURE() << "winner's canceller must never be invoked";
      co_return;
    };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(winner(), neverRun), // callable; winner -> never invoked
      tmc::cancellable(loser(block), [&] { block.inc(); })
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

// Overload 1 with a *callable that returns a task*: select() invokes the
// canceller, then co_awaits the task it returns (an asynchronous cancel).
TEST_F(CATEGORY, select_function_canceller_returns_task) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);

    auto winner = []() -> tmc::task<void> { co_return; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(loser(block), [&block]() -> tmc::task<void> {
        block.inc();
        co_return;
      })
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

// Overload 2 with an object whose .cancel() is *asynchronous* (returns a task).
// select() co_awaits the task returned by the synthesized canceller.
TEST_F(CATEGORY, select_cancellable_object_async) {
  test_async_main(ex(), []() -> tmc::task<void> {
    struct async_event_canceller {
      atomic_awaitable<int>& block;
      tmc::task<void> cancel() {
        block.inc();
        co_return;
      }
    };
    atomic_awaitable<int> block(1);
    async_event_canceller stopper{block};

    auto winner = []() -> tmc::task<int> { co_return 5; };
    auto loser = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 6;
    };

    auto result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(loser(block), stopper) // object with awaitable .cancel()
    );

    EXPECT_EQ(result.index(), 0u);
    EXPECT_EQ(std::get<0>(result), 5);
  }());
}

// cancel_noop is the always-ready, no-op awaiter that select() yields for a
// synchronous canceller mixed into its asynchronous-cancel branch. Because its
// await_ready() returns true, a normal co_await never reaches await_suspend(),
// so exercise the full awaiter interface directly to document and cover its
// (deliberately empty) contract.
TEST_F(CATEGORY, select_cancel_noop_awaiter) {
  tmc::detail::cancel_noop noop;
  EXPECT_TRUE(noop.await_ready());
  noop.await_suspend(std::noop_coroutine());
  noop.await_resume();
}

// Overload 3 (self-cancel) with an *asynchronous* .cancel(): the op is both the
// awaitable and its own cancel handle, and its .cancel() returns a task that
// select() co_awaits to cancel it in place, then drains.
TEST_F(CATEGORY, select_self_cancellable_async) {
  test_async_main(ex(), []() -> tmc::task<void> {
    async_cancellable_op op;

    auto winner = []() -> tmc::task<void> { co_return; };

    std::variant<std::monostate, std::monostate> result = co_await tmc::select(
      tmc::cancellable(winner(), [] {}),
      tmc::cancellable(op) // single-argument overload, asynchronous .cancel()
    );

    EXPECT_EQ(result.index(), 0u);
  }());
}

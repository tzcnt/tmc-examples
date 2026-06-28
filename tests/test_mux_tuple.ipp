#include "atomic_awaitable.hpp"
#include "test_common.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <cassert>
#include <coroutine>
#include <cstddef>
#include <optional>
#include <thread>
#include <type_traits>
#include <variant>

// Tests for tmc::mux_tuple - a standalone result-multiplexer that initiates a
// set of awaitables like tmc::spawn_tuple() but yields each result as it becomes
// ready, and also supports an empty constructor (storage only) plus restart<I>()
// to (re)launch work into individual slots.
//
// restart<I>() (re)starts a slot of the group with a new awaitable. The
// awaitable is initiated immediately (synchronously inside restart()) and joined
// by a later co_await of the group. These tests exercise every value category
// (lvalue, movable rvalue, non-movable rvalue) and every runtime mode of
// tmc::detail::awaitable_traits<T>::mode that restart() can be handed (TMC_TASK,
// COROUTINE, ASYNC_INITIATE - both lvalue- and rvalue-qualified - and WRAPPER).
// UNKNOWN is the not-an-awaitable case and is rejected by a static_assert inside
// restart(), so it is not runtime-testable.
//
// The restart() tests use a deterministic skeleton: slot 0 is an
// immediately-ready awaitable that is consumed and then restarted; slot 1 blocks
// on an atomic_awaitable until the test releases it, so the group never reports
// slot 1 before the restart has been observed.

// A WRAPPER-mode awaitable: it implements the await_ready/await_suspend/
// await_resume interface directly and has no awaitable_traits specialization, so
// mux_tuple must route it through safe_wrap(). Immediately ready; yields a stored
// int. Its await_* are const, so it is re-awaitable and works whether borrowed
// as an lvalue or moved as an rvalue.
struct mux_wrapper_int {
  int v;
  bool await_ready() const noexcept { return true; }
  void await_suspend(std::coroutine_handle<>) const noexcept {}
  int await_resume() const noexcept { return v; }
};

// A COROUTINE-mode awaitable: it wraps a tmc::task and is convertible to a
// std::coroutine_handle<>, so mux_tuple resumes it as a raw coroutine (the
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
// restart() and remains valid to drive in place. Completion is driven by
// complete(). Mirrors the rvalue_cancellable_op used by the select() tests, but
// is defined here so this file is self-contained.
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

/*** Eager constructor (initiates all awaitables like spawn_tuple()) ***/

// Basic eager construction with coroutine awaitables: drain every slot once.
TEST_F(CATEGORY, mux_tuple_eager_each) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int I) -> tmc::task<int> { co_return 1 << I; };
    tmc::mux_tuple mux(work(0), work(1), work(2));

    int sum = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      switch (i) {
      case 0:
        sum += mux.get<0>();
        break;
      case 1:
        sum += mux.get<1>();
        break;
      case 2:
        sum += mux.get<2>();
        break;
      default:
        ADD_FAILURE() << "invalid index: " << i;
        break;
      }
    }
    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

// Eager construction where every awaitable is ASYNC_INITIATE (no coroutines).
// Exercises the WorkItemCount == 0 path (no bulk submit) and the individual
// async_initiate loop in the constructor.
TEST_F(CATEGORY, mux_tuple_eager_all_async_initiate) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> a(1);
    atomic_awaitable<int> b(1);
    tmc::mux_tuple mux(a, b); // both ASYNC_INITIATE -> WorkItemCount == 0
    a.inc();
    b.inc();

    int count = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      ++count;
    }
    EXPECT_EQ(count, 2);
  }());
}

// Eager construction mixing a WRAPPER-mode awaitable with a task. The WRAPPER is
// routed through into_known()/safe_wrap() in the constructor (and counted as a
// coroutine / bulk-submitted work item).
TEST_F(CATEGORY, mux_tuple_eager_with_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 7;
    };

    tmc::mux_tuple mux(mux_wrapper_int{5}, blocker(block)); // WRAPPER + TMC_TASK

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 5);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    EXPECT_EQ(mux.get<1>(), 7);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Every eagerly-initiated task completes (awaited via `aa`) before the group is
// drained, so the first co_await of the group finds all results already ready.
TEST_F(CATEGORY, mux_tuple_eager_resume_after) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto make_task = [](int I, atomic_awaitable<size_t>& AA) -> tmc::task<int> {
      AA.inc();
      co_return 1 << I;
    };
    static constexpr int N = 5;
    atomic_awaitable<size_t> aa(N);
    tmc::mux_tuple mux(
      make_task(0, aa), make_task(1, aa), make_task(2, aa), make_task(3, aa),
      make_task(4, aa)
    );
    co_await aa;
    int sum = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      switch (idx) {
      case 0:
        sum += mux.get<0>();
        break;
      case 1:
        sum += mux.get<1>();
        break;
      case 2:
        sum += mux.get<2>();
        break;
      case 3:
        sum += mux.get<3>();
        break;
      case 4:
        sum += mux.get<4>();
        break;
      }
    }

    EXPECT_EQ(sum, (1 << N) - 1);

    co_return;
  }());
}

// Regression: CTAD on the eager constructor must apply forward_awaitable and
// preserve the value category of a non-movable rvalue awaitable - storing it by
// reference (mux_rvalue_async_op&&), not collapsing it to a by-value tuple
// element (which is non-movable and would fail to compile). This guards the
// constrained deduction guide: without the constraint, the eager constructor's
// own (more-constrained) implicit guide would win and drop forward_awaitable.
// `op` is declared before `mux` so it outlives the group that borrows it.
TEST_F(CATEGORY, mux_tuple_eager_ctad_non_movable_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto immediate = []() -> tmc::task<void> { co_return; };
    mux_rvalue_async_op op; // non-movable, rvalue-qualified ASYNC_INITIATE
    tmc::mux_tuple mux(immediate(), std::move(op));
    static_assert(std::is_same_v<
                  decltype(mux), tmc::mux_tuple<tmc::task<void>, mux_rvalue_async_op&&>>);
    op.complete();
    int count = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      ++count;
    }
    EXPECT_EQ(count, 2);
  }());
}

/*** Empty constructor (storage only; restart<I>() to launch work) ***/

// The empty constructor allocates result storage but initiates nothing. The
// template arguments must be provided explicitly. restart<I>() then launches
// each slot, and slot 0 is restarted again after its first result is consumed.
TEST_F(CATEGORY, mux_tuple_empty_constructor_restart_all) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 99;
    };

    tmc::mux_tuple<tmc::task<int>, tmc::task<int>> mux; // storage only
    mux.restart<0>(immediate(1));
    mux.restart<1>(blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.restart<0>(immediate(2));
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 2);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    EXPECT_EQ(mux.get<1>(), 99);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// An empty mux_tuple that is drained without ever starting a slot: the first
// co_await immediately reports end() (remaining_count == 0). Exercises the
// remaining_count == 0 path in await_suspend()/await_resume().
TEST_F(CATEGORY, mux_tuple_empty_drain) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mux_tuple<tmc::task<int>> mux;
    size_t idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Demonstrates the core use case: maintain a fixed level of concurrency by
// restarting a slot with new work as soon as its previous result is consumed.
TEST_F(CATEGORY, mux_tuple_restart_maintains_concurrency) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int V) -> tmc::task<int> { co_return V; };

    tmc::mux_tuple<tmc::task<int>, tmc::task<int>> mux;
    mux.restart<0>(work(0));
    mux.restart<1>(work(1));

    int produced = 0;
    int sum = 0;
    // Consume 6 results total, feeding new work back into whichever slot
    // completed, then drain the final two in-flight tasks.
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      if (i == 0) {
        sum += mux.get<0>();
      } else {
        sum += mux.get<1>();
      }
      ++produced;
      if (produced < 4) {
        int next = (produced + 1) * 10;
        if (i == 0) {
          mux.restart<0>(work(next));
        } else {
          mux.restart<1>(work(next));
        }
      }
    }
    // First two results: 0 + 1. Then four restarts produced for produced in
    // {1,2,3}: values 20, 30, 40 (3 restarts). 0+1+20+30+40 = 91.
    EXPECT_EQ(produced, 5);
    EXPECT_EQ(sum, 0 + 1 + 20 + 30 + 40);
  }());
}

/*** restart<I>() value categories and awaitable modes ***/

// Mode TMC_TASK, movable rvalue replacement. The replacement tmc::task is a
// prvalue, so it is moved into restart().
TEST_F(CATEGORY, mux_tuple_restart_task_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.restart<0>(immediate(4)); // TMC_TASK, movable rvalue
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 4);

    block.inc(); // release slot 1
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode TMC_TASK, void result. The replaced slot is a std::monostate.
TEST_F(CATEGORY, mux_tuple_restart_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    [[maybe_unused]] std::monostate m0 = mux.get<0>(); // void -> monostate slot

    mux.restart<0>(immediate()); // TMC_TASK, void
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    [[maybe_unused]] std::monostate m1 = mux.get<0>();

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode TMC_TASK, non-default-constructible result. The slot is wrapped in a
// std::optional, exercising restart()'s ResultStorage match (the static_assert)
// and the optional-wrapped slot.
TEST_F(CATEGORY, mux_tuple_restart_non_default_constructible) {
  struct NoDefault {
    int v;
    NoDefault() = delete;
    explicit NoDefault(int V) : v(V) {}
  };
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<NoDefault> { co_return NoDefault{V}; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<NoDefault> {
      co_await B;
      co_return NoDefault{0};
    };

    tmc::mux_tuple mux(immediate(7), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>()->v, 7); // get<0>() is std::optional<NoDefault>

    mux.restart<0>(immediate(11));
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>()->v, 11);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode COROUTINE, movable rvalue replacement.
TEST_F(CATEGORY, mux_tuple_restart_coroutine_mode) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.restart<0>(mux_coroutine_op<int>{immediate(4)}); // COROUTINE, rvalue
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 4);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode WRAPPER, movable rvalue replacement. safe_wrap() moves the awaitable into
// the wrapper coroutine (consume-once).
TEST_F(CATEGORY, mux_tuple_restart_wrapper_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.restart<0>(mux_wrapper_int{4}); // WRAPPER, movable rvalue
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 4);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode WRAPPER, lvalue replacement. safe_wrap() borrows the awaitable by
// reference and awaits it as an lvalue (re-awaitable); the lvalue must outlive
// the group, so it is a named local kept alive until the drain completes.
TEST_F(CATEGORY, mux_tuple_restart_wrapper_lvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux_wrapper_int w{4};
    mux.restart<0>(w); // WRAPPER, lvalue (borrowed by reference)
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 4);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode ASYNC_INITIATE, lvalue-qualified (async_initiate(self_type&)). The
// replacement is borrowed as an lvalue and driven to completion via inc().
TEST_F(CATEGORY, mux_tuple_restart_async_initiate_lvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    // atomic_awaitable is an lvalue-qualified ASYNC_INITIATE awaitable (void
    // result -> monostate slot, matching the void task it replaces).
    atomic_awaitable<int> repl(1);
    mux.restart<0>(repl); // lvalue
    repl.inc();           // drive completion
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode ASYNC_INITIATE, rvalue-qualified (async_initiate(self_type&&)). The
// replacement is a non-movable, consume-once awaitable; it is passed as an
// rvalue but borrowed in place (it can't be moved) and so is a named local kept
// alive until the drain completes, driven via complete().
TEST_F(CATEGORY, mux_tuple_restart_async_initiate_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    mux_rvalue_async_op repl;        // rvalue-qualified, non-movable, void result
    mux.restart<0>(std::move(repl)); // rvalue
    repl.complete();                 // drive completion
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "test_mux_common.hpp"

#include <gtest/gtest.h>

#include <cassert>
#include <cstddef>
#include <variant>

// Tests for tmc::mux_tuple - a standalone result-multiplexer that initiates a
// set of awaitables like tmc::spawn_tuple() but yields each result as it becomes
// ready, and also supports an empty constructor (storage only) plus fork<I>()
// to (re)launch work into individual slots.
//
// fork<I>() (re)starts a slot of the group with a new awaitable. The
// awaitable is initiated immediately (synchronously inside fork()) and joined
// by a later co_await of the group. These tests exercise every value category
// (lvalue, movable rvalue, non-movable rvalue) and every runtime mode of
// tmc::detail::awaitable_traits<T>::mode that fork() can be handed (TMC_TASK,
// COROUTINE, ASYNC_INITIATE - both lvalue- and rvalue-qualified - and WRAPPER).
// UNKNOWN is the not-an-awaitable case and is rejected by a static_assert inside
// fork(), so it is not runtime-testable.
//
// The fork() tests use a deterministic skeleton: slot 0 is an
// immediately-ready awaitable that is consumed and then forked; slot 1 blocks
// on an atomic_awaitable until the test releases it, so the group never reports
// slot 1 before the fork has been observed.

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

// Regression: the eager constructor must apply forward_awaitable and preserve
// the value category of a non-movable rvalue awaitable - storing it by reference
// internally, not collapsing it to a by-value tuple element (which is
// non-movable and would fail to compile). CTAD deduces the slot result types
// (here both void), so the deduced type erases how each awaitable is stored;
// constructing from a non-movable rvalue without crashing is itself the
// compile-time check that the by-reference storage is preserved.
// `op` is declared before `mux` so it outlives the group that borrows it.
TEST_F(CATEGORY, mux_tuple_eager_ctad_non_movable_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto immediate = []() -> tmc::task<void> { co_return; };
    mux_rvalue_async_op op; // non-movable, rvalue-qualified ASYNC_INITIATE
    tmc::mux_tuple mux(immediate(), std::move(op));
    static_assert(std::is_same_v<decltype(mux), tmc::mux_tuple<void, void>>);
    op.complete();
    int count = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      ++count;
    }
    EXPECT_EQ(count, 2);
  }());
}

// The mux captures its continuation executor at each co_await, not at
// construction. Construct (and eagerly initiate on) ex(), then move to a
// second executor before awaiting: every co_await must resume on the second
// executor, including results from slots forked while running there.
TEST_F(CATEGORY, mux_tuple_await_on_second_executor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu exec2;
    exec2.set_thread_count(1).init();

    tmc::mux_tuple mux(work(0), work(1));

    co_await tmc::resume_on(exec2);
    EXPECT_EQ(tmc::current_executor(), exec2.type_erased());

    int sum = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      EXPECT_EQ(tmc::current_executor(), exec2.type_erased());
      switch (i) {
      case 0:
        sum += mux.get<0>();
        break;
      case 1:
        sum += mux.get<1>();
        break;
      default:
        ADD_FAILURE() << "unexpected slot index " << i;
        break;
      }
    }
    // The end()-returning co_await also resumes here.
    EXPECT_EQ(tmc::current_executor(), exec2.type_erased());
    EXPECT_EQ(sum, 3);

    // Fork a replacement that runs on ex(); its completion must still resume
    // the awaiter on exec2.
    mux.fork<0>(work(2), ex());
    size_t i = co_await mux;
    EXPECT_EQ(tmc::current_executor(), exec2.type_erased());
    EXPECT_EQ(i, 0u);
    EXPECT_EQ(mux.get<0>(), 4);
    i = co_await mux;
    EXPECT_EQ(i, mux.end());

    co_await tmc::resume_on(ex());
  }());
}

// capacity() reports the fixed number of slots, equal to the number of `Result`
// template arguments. It is independent of how many slots are currently active
// and stays constant as the group is forked and drained.
TEST_F(CATEGORY, mux_tuple_capacity) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Empty (storage-only) group: capacity reflects the template arity, not the
    // number of forked/active slots (none are active here).
    tmc::mux_tuple<int, int, int> mux3;
    EXPECT_EQ(mux3.capacity(), 3u);
    EXPECT_EQ(mux3.active_bitset(), 0u);

    // Single-slot group.
    tmc::mux_tuple<int> mux1;
    EXPECT_EQ(mux1.capacity(), 1u);

    // Eager (CTAD) group: capacity equals the deduced arity and does not change
    // as the slots are drained.
    tmc::mux_tuple mux2(work(0), work(1));
    EXPECT_EQ(mux2.capacity(), 2u);
    int count = 0;
    for (size_t i = co_await mux2; i != mux2.end(); i = co_await mux2) {
      ++count;
      EXPECT_EQ(mux2.capacity(), 2u); // unchanged while draining
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(mux2.capacity(), 2u);
  }());
}

/*** Empty constructor (storage only; fork<I>() to launch work) ***/

// The empty constructor allocates result storage but initiates nothing. The
// template arguments must be provided explicitly. fork<I>() then launches
// each slot, and slot 0 is forked again after its first result is consumed.
TEST_F(CATEGORY, mux_tuple_empty_constructor_fork_all) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 99;
    };

    tmc::mux_tuple<int, int> mux; // storage only
    mux.fork<0>(immediate(1));
    mux.fork<1>(blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.fork<0>(immediate(2));
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
    tmc::mux_tuple<int> mux;
    size_t idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Demonstrates the core use case: maintain a fixed level of concurrency by
// forking a slot with new work as soon as its previous result is consumed.
TEST_F(CATEGORY, mux_tuple_fork_maintains_concurrency) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int V) -> tmc::task<int> { co_return V; };

    tmc::mux_tuple<int, int> mux;
    mux.fork<0>(work(0));
    mux.fork<1>(work(1));

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
          mux.fork<0>(work(next));
        } else {
          mux.fork<1>(work(next));
        }
      }
    }
    // First two results: 0 + 1. Then four forks produced for produced in
    // {1,2,3}: values 20, 30, 40 (3 forks). 0+1+20+30+40 = 91.
    EXPECT_EQ(produced, 5);
    EXPECT_EQ(sum, 0 + 1 + 20 + 30 + 40);
  }());
}

// fork() accepts an optional executor and priority used to dispatch the
// awaitable. Here both are given explicitly (the fixture's own executor at
// priority 0); this exercises the explicit-argument overload on every executor
// type the fixture set runs under. (Cross-executor and cross-priority dispatch is
// verified more thoroughly in test_prio.cpp.)
TEST_F(CATEGORY, mux_tuple_fork_explicit_executor_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int V) -> tmc::task<int> { co_return V; };
    tmc::mux_tuple<int, int> mux;
    mux.fork<0>(work(1), ex(), 0);
    mux.fork<1>(work(2), ex(), 0);

    int sum = 0;
    int count = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      sum += (i == 0) ? mux.get<0>() : mux.get<1>();
      ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 3);
  }());
}

/*** fork<I>() value categories and awaitable modes ***/

// Mode TMC_TASK, movable rvalue replacement. The replacement tmc::task is a
// prvalue, so it is moved into fork().
TEST_F(CATEGORY, mux_tuple_fork_task_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.fork<0>(immediate(4)); // TMC_TASK, movable rvalue
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
TEST_F(CATEGORY, mux_tuple_fork_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    [[maybe_unused]] std::monostate m0 = mux.get<0>(); // void -> monostate slot

    mux.fork<0>(immediate()); // TMC_TASK, void
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
// std::optional, exercising fork()'s ResultStorage match (the static_assert)
// and the optional-wrapped slot.
TEST_F(CATEGORY, mux_tuple_fork_non_default_constructible) {
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
    // get<0>() unwraps the std::optional and returns NoDefault&.
    EXPECT_EQ(mux.get<0>().v, 7);

    mux.fork<0>(immediate(11));
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>().v, 11);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode COROUTINE, movable rvalue replacement.
TEST_F(CATEGORY, mux_tuple_fork_coroutine_mode) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.fork<0>(mux_coroutine_op<int>{immediate(4)}); // COROUTINE, rvalue
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
TEST_F(CATEGORY, mux_tuple_fork_wrapper_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux.fork<0>(mux_wrapper_int{4}); // WRAPPER, movable rvalue
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
TEST_F(CATEGORY, mux_tuple_fork_wrapper_lvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(1), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux.get<0>(), 1);

    mux_wrapper_int w{4};
    mux.fork<0>(w); // WRAPPER, lvalue (borrowed by reference)
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
TEST_F(CATEGORY, mux_tuple_fork_async_initiate_lvalue) {
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
    mux.fork<0>(repl); // lvalue
    repl.inc();        // drive completion
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
TEST_F(CATEGORY, mux_tuple_fork_async_initiate_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    tmc::mux_tuple mux(immediate(), blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    mux_rvalue_async_op repl;     // rvalue-qualified, non-movable, void result
    mux.fork<0>(std::move(repl)); // rvalue
    repl.complete();              // drive completion
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

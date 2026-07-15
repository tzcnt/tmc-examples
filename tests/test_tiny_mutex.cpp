// Tests for tmc::tiny_mutex, the internal serializing primitive used by the
// tmc-asio safe_* objects. Ownership is held by the runner rather than the
// acquiring coroutine, and is released when the acquiring coroutine next
// suspends.

#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/detail/tiny_mutex.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/ex_cpu_st.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <utility>

#define CATEGORY test_tiny_mutex

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, is_locked) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::tiny_mutex mut;
    EXPECT_FALSE(mut.is_locked());
    co_await mut;
    // This coroutine is currently being run by the runner.
    EXPECT_TRUE(mut.is_locked());
    co_await mut.co_unlock();
    // After co_unlock the runner may still be draining; no assertion here.
  }());
}

// The critical section is the synchronous stretch between `co_await mut` and
// the next suspension. Two coroutines must never be in it simultaneously.
static tmc::task<void>
excl_worker(tmc::tiny_mutex& Mut, int& Counter, std::atomic<int>& InCs) {
  for (int i = 0; i < 1000; ++i) {
    co_await Mut;
    int prev = InCs.fetch_add(1, std::memory_order_relaxed);
    EXPECT_EQ(prev, 0);
    // Deliberately non-atomic update; only safe if exclusion holds.
    int v = Counter;
    Counter = v + 1;
    InCs.fetch_sub(1, std::memory_order_relaxed);
    co_await Mut.co_unlock();
  }
}

TEST_F(CATEGORY, mutual_exclusion) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::tiny_mutex mut;
    int counter = 0;
    std::atomic<int> inCs{0};
    co_await tmc::spawn_many<8>(tmc::iter_adapter(
      0, [&](int) -> tmc::task<void> { return excl_worker(mut, counter, inCs); }
    ));
    EXPECT_EQ(counter, 8 * 1000);
  }());
}

static tmc::task<int> locked_return_rvalue(tmc::tiny_mutex& Mut) {
  co_await Mut;
  co_await Mut.co_unlock_return(42);
  TMC_UNREACHABLE;
}

static tmc::task<int> locked_return_lvalue(tmc::tiny_mutex& Mut, int& Counter) {
  co_await Mut;
  int v = ++Counter;
  co_await Mut.co_unlock_return(v);
  TMC_UNREACHABLE;
}

static tmc::task<void> locked_return_void(tmc::tiny_mutex& Mut, int& Counter) {
  co_await Mut;
  ++Counter;
  co_await Mut.co_unlock_return();
  TMC_UNREACHABLE;
}

TEST_F(CATEGORY, co_unlock_return) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::tiny_mutex mut;
    int counter = 0;
    auto rv = co_await locked_return_rvalue(mut);
    EXPECT_EQ(rv, 42);
    auto lv = co_await locked_return_lvalue(mut, counter);
    EXPECT_EQ(lv, 1);
    co_await locked_return_void(mut, counter);
    EXPECT_EQ(counter, 2);
  }());
}

// co_unlock_return provides mutual exclusion for the whole method body, the
// same pattern used by the safe_* asio objects' synchronous methods.
static tmc::task<void>
value_worker(tmc::tiny_mutex& Mut, int& Counter, std::atomic<int>& Sum) {
  for (int i = 0; i < 500; ++i) {
    int v = co_await locked_return_lvalue(Mut, Counter);
    Sum.fetch_add(v, std::memory_order_relaxed);
  }
}

TEST_F(CATEGORY, co_unlock_return_contended) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::tiny_mutex mut;
    int counter = 0;
    std::atomic<int> sum{0};
    co_await tmc::spawn_many<8>(tmc::iter_adapter(
      0,
      [&](int) -> tmc::task<void> { return value_worker(mut, counter, sum); }
    ));
    int n = 8 * 500;
    EXPECT_EQ(counter, n);
    // If every increment was exclusive, the returned values are exactly 1..N.
    EXPECT_EQ(sum.load(), n * (n + 1) / 2);
  }());
}

// After both lock acquisition and unlock, the coroutine must be back on its
// own executor at its own priority, even under cross-executor contention.
static tmc::task<void> check_exec_prio_worker(tmc::tiny_mutex& Mut) {
  auto* execBefore = tmc::current_executor();
  auto prioBefore = tmc::current_priority();
  for (int i = 0; i < 100; ++i) {
    co_await Mut;
    EXPECT_EQ(tmc::current_executor(), execBefore);
    EXPECT_EQ(tmc::current_priority(), prioBefore);
    co_await Mut.co_unlock();
    EXPECT_EQ(tmc::current_executor(), execBefore);
    EXPECT_EQ(tmc::current_priority(), prioBefore);
  }
}

TEST_F(CATEGORY, executor_priority_restored) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::tiny_mutex mut;
    tmc::ex_cpu_st localEx;
    localEx.init();
    auto local =
      tmc::spawn_many<4>(tmc::iter_adapter(
                           0,
                           [&](int) -> tmc::task<void> {
                             return check_exec_prio_worker(mut);
                           }
      ))
        .run_on(localEx)
        .fork();
    co_await tmc::spawn_many<4>(tmc::iter_adapter(
                                  0,
                                  [&](int) -> tmc::task<void> {
                                    return check_exec_prio_worker(mut);
                                  }
    ))
      .with_priority(1);
    co_await std::move(local);
  }());
}

// Re-locking releases at the suspend point and re-queues; the runner is still
// active at that moment.
static tmc::task<void> relock_worker(tmc::tiny_mutex& Mut, int& Counter) {
  for (int i = 0; i < 500; ++i) {
    co_await Mut;
    ++Counter;
    co_await Mut;
    ++Counter;
    co_await Mut.co_unlock();
  }
}

TEST_F(CATEGORY, relock) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::tiny_mutex mut;
    int counter = 0;
    co_await tmc::spawn_many<4>(tmc::iter_adapter(
      0, [&](int) -> tmc::task<void> { return relock_worker(mut, counter); }
    ));
    EXPECT_EQ(counter, 2 * 4 * 500);
  }());
}

static tmc::task<void> lock_and_plain_return(tmc::tiny_mutex& Mut, int& Hits) {
  co_await Mut;
  ++Hits;
  // No explicit unlock: the lock is released when control returns to the
  // runner after this task completes.
  co_return;
}

// The refcounted state allows the mutex to be destroyed while the runner is
// still winding down, as long as all queued work has completed.
TEST_F(CATEGORY, construct_destroy_churn) {
  test_async_main(ex(), []() -> tmc::task<void> {
    for (int i = 0; i < 200; ++i) {
      int hits = 0;
      {
        tmc::tiny_mutex mut;
        co_await tmc::spawn_many<4>(tmc::iter_adapter(
          0,
          [&](int) -> tmc::task<void> {
            return lock_and_plain_return(mut, hits);
          }
        ));
      }
      EXPECT_EQ(hits, 4);
    }
  }());
}

#undef CATEGORY

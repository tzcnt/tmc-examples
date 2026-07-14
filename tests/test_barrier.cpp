#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/barrier.hpp"
#include "tmc/current.hpp"
#include "waiter_count_accessor.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <optional>
#include <vector>

#define CATEGORY test_barrier

class CATEGORY : public testing::Test {
protected:
  // A second executor, used to exercise waking waiters that resume on
  // different executors.
  static inline tmc::ex_cpu otherExec;

  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
    otherExec.set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() {
    otherExec.teardown();
    tmc::cpu_executor().teardown();
  }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }

  using waiter_count_accessor = tmc::tests::waiter_count_accessor;
};

static tmc::task<void> waiter(
  tmc::barrier& B, std::vector<std::atomic<bool>>& DoneArray, size_t DoneIdx
) {
  DoneArray[DoneIdx].store(true, std::memory_order_relaxed);
  co_await B;
  // Each waiter should see all other waiters as done after the barrier has been
  // passed.
  for (auto& d : DoneArray) {
    EXPECT_EQ(d.load(std::memory_order_relaxed), true);
  }
}

// Alternates between modification and verification phases
static tmc::task<void> flip_flop_waiter(
  tmc::barrier& B, std::vector<std::atomic<bool>>& DoneArray, size_t DoneIdx
) {
  for (size_t i = 0; i < 10; ++i) {
    DoneArray[DoneIdx].store(true, std::memory_order_relaxed);
    co_await B;

    for (auto& d : DoneArray) {
      EXPECT_EQ(d.load(std::memory_order_relaxed), true);
    }
    co_await B;

    DoneArray[DoneIdx].store(false, std::memory_order_relaxed);
    co_await B;

    for (auto& d : DoneArray) {
      EXPECT_EQ(d.load(std::memory_order_relaxed), false);
    }
    co_await B;
  }
}

// Waits on the barrier, then increments AA when resumed.
static tmc::task<void>
barrier_wait_and_inc(tmc::barrier& B, atomic_awaitable<int>& AA) {
  co_await B;
  AA.inc();
}

// Waits on the barrier, then asserts it resumed on ExpectedExec before
// incrementing AA.
static tmc::task<void> barrier_wait_check_exec_and_inc(
  tmc::barrier& B, atomic_awaitable<int>& AA, tmc::ex_any* ExpectedExec
) {
  co_await B;
  EXPECT_EQ(tmc::current_executor(), ExpectedExec);
  AA.inc();
}

// Waits on the barrier, then asserts it resumed at ExpectedPrio before
// incrementing AA.
static tmc::task<void> barrier_wait_check_prio_and_inc(
  tmc::barrier& B, atomic_awaitable<int>& AA, size_t ExpectedPrio
) {
  co_await B;
  EXPECT_EQ(tmc::current_priority(), ExpectedPrio);
  AA.inc();
}

TEST_F(CATEGORY, zero_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(0);
    co_await bar;
    co_await bar;
  }());
}

TEST_F(CATEGORY, negative_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(static_cast<size_t>(-1));
    co_await bar;
    co_await bar;
  }());
}

TEST_F(CATEGORY, one_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(static_cast<size_t>(1));
    co_await bar;
    co_await bar;
  }());
}

TEST_F(CATEGORY, once) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(5);
    std::vector<tmc::task<void>> tasks(5);
    std::vector<std::atomic<bool>> dones(5);
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);
  }());
}

TEST_F(CATEGORY, auto_reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(5);
    std::vector<tmc::task<void>> tasks(5);
    std::vector<std::atomic<bool>> dones(5);
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);

    for (size_t i = 0; i < tasks.size(); ++i) {
      dones[i].store(false, std::memory_order_relaxed);
      tasks[i] = waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);
  }());
}

TEST_F(CATEGORY, flip_flop) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::barrier bar(5);
    std::vector<tmc::task<void>> tasks(5);
    std::vector<std::atomic<bool>> dones(5);
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = flip_flop_waiter(bar, dones, i);
    }
    co_await tmc::spawn_many(tasks);
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::barrier> bar;
    bar.emplace(100);
    auto t =
      tmc::spawn(
        [](tmc::barrier& Bar, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Bar;
          AA.inc();
        }(*bar, aa)
      )
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(*bar, 1);
    EXPECT_EQ(aa.load(), 0);
    // Destroy bar while the task is still waiting.
    bar.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

// Wake more than one batch worth of waiters (the batch size is 64). The final
// waiter to arrive resumes by symmetric transfer; the remainder are posted to
// the executor as a full batch followed by a partial trailing batch.
TEST_F(CATEGORY, many_waiters) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // 200 spans three full batches (192) plus a partial trailing batch.
    static constexpr size_t COUNT = 200;
    tmc::barrier bar(COUNT);
    atomic_awaitable<int> aa(COUNT);
    std::vector<tmc::task<void>> tasks(COUNT);
    for (size_t i = 0; i < COUNT; ++i) {
      tasks[i] = barrier_wait_and_inc(bar, aa);
    }
    // The barrier fires automatically once all COUNT waiters have arrived.
    auto t = tmc::spawn_many(tasks.data(), COUNT).fork();
    co_await aa;
    EXPECT_EQ(aa.load(), static_cast<int>(COUNT));
    co_await std::move(t);
  }());
}

// Wake waiters that resume on different executors. A batch can only be posted
// to a single executor, so a new batch must be started whenever the executor
// changes.
TEST_F(CATEGORY, different_executors) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Enough waiters per executor that the interleaving also crosses a batch
    // boundary (64).
    static constexpr size_t PER = 50;
    tmc::barrier bar(2 * PER);
    atomic_awaitable<int> aa(2 * PER);
    std::vector<tmc::task<void>> tasksA(PER);
    std::vector<tmc::task<void>> tasksB(PER);
    for (size_t i = 0; i < PER; ++i) {
      tasksA[i] =
        barrier_wait_check_exec_and_inc(bar, aa, tmc::cpu_executor().type_erased());
      tasksB[i] = barrier_wait_check_exec_and_inc(bar, aa, otherExec.type_erased());
    }
    auto ta = tmc::spawn_many(tasksA.data(), PER).run_on(tmc::cpu_executor()).fork();
    auto tb = tmc::spawn_many(tasksB.data(), PER).run_on(otherExec).fork();
    co_await aa;
    EXPECT_EQ(aa.load(), static_cast<int>(2 * PER));
    co_await std::move(ta);
    co_await std::move(tb);
  }());
}

// Wake waiters that resume at different priorities. A batch can only be posted
// at a single priority, so a new batch must be started whenever the priority
// changes.
TEST_F(CATEGORY, different_priorities) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Enough waiters per priority that the interleaving also crosses a batch
    // boundary (64).
    static constexpr size_t PER = 50;
    tmc::barrier bar(2 * PER);
    atomic_awaitable<int> aa(2 * PER);
    std::vector<tmc::task<void>> tasks0(PER);
    std::vector<tmc::task<void>> tasks1(PER);
    for (size_t i = 0; i < PER; ++i) {
      tasks0[i] = barrier_wait_check_prio_and_inc(bar, aa, 0);
      tasks1[i] = barrier_wait_check_prio_and_inc(bar, aa, 1);
    }
    auto t0 = tmc::spawn_many(tasks0.data(), PER).with_priority(0).fork();
    auto t1 = tmc::spawn_many(tasks1.data(), PER).with_priority(1).fork();
    co_await aa;
    EXPECT_EQ(aa.load(), static_cast<int>(2 * PER));
    co_await std::move(t0);
    co_await std::move(t1);
  }());
}

#undef CATEGORY

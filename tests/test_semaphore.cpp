#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/semaphore.hpp"
#include "tmc/utils.hpp"
#include "waiter_count_accessor.hpp"

#include <gtest/gtest.h>

#include <array>

#define CATEGORY test_semaphore

class CATEGORY : public testing::Test {
protected:
  using waiter_count_accessor = tmc::tests::waiter_count_accessor;

  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, no_waiters) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    EXPECT_EQ(sem.count(), 0);
    sem.release();
    EXPECT_EQ(sem.count(), 1);
    co_return;
  }());
}

TEST_F(CATEGORY, no_waiters_co_release) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    EXPECT_EQ(sem.count(), 0);
    co_await sem.co_release();
    EXPECT_EQ(sem.count(), 1);
    co_return;
  }());
}

TEST_F(CATEGORY, nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);
    EXPECT_EQ(sem.count(), 1);
    co_await sem;
    EXPECT_EQ(sem.count(), 0);
    sem.release();
    EXPECT_EQ(sem.count(), 1);
    sem.release(2);
    EXPECT_EQ(sem.count(), 3);
    co_await sem;
    EXPECT_EQ(sem.count(), 2);
    co_await sem;
    EXPECT_EQ(sem.count(), 1);
    {
      auto s = co_await sem.acquire_scope();
      EXPECT_EQ(sem.count(), 0);
    }
    EXPECT_EQ(sem.count(), 1);
    {
      tmc::semaphore_scope s{co_await sem.acquire_scope()};
      EXPECT_EQ(sem.count(), 0);
    }
    EXPECT_EQ(sem.count(), 1);
    co_await sem;
    EXPECT_EQ(sem.count(), 0);
  }());
}

TEST_F(CATEGORY, try_acquire) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(2);
    // Succeeds while resources are available, draining the count.
    EXPECT_EQ(sem.try_acquire(), true);
    EXPECT_EQ(sem.count(), 1);
    EXPECT_EQ(sem.try_acquire(), true);
    EXPECT_EQ(sem.count(), 0);
    // Fails when no resources are available.
    EXPECT_EQ(sem.try_acquire(), false);
    EXPECT_EQ(sem.count(), 0);

    // Releasing makes a resource available again.
    sem.release();
    EXPECT_EQ(sem.count(), 1);
    EXPECT_EQ(sem.try_acquire(), true);
    EXPECT_EQ(sem.count(), 0);

    // With a waiter queued, try_acquire cannot barge ahead of it. Releasing
    // transfers the resource to the waiter, leaving count at 0.
    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Sem;
        AA.inc();
      }(sem, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);
    EXPECT_EQ(sem.try_acquire(), false);
    sem.release();
    co_await aa;
    co_await std::move(t);
    EXPECT_EQ(sem.count(), 0);
  }());
}

TEST_F(CATEGORY, one_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);
    co_await sem;

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Sem;
        AA.inc();
      }(sem, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    sem.release();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, multi_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(5);
    std::array<tmc::task<void>, 5> tasks;
    for (size_t i = 0; i < 5; ++i) {
      tasks[i] = [](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Sem;
        AA.inc();
      }(sem, aa);
    }
    auto t = tmc::spawn_many(tasks).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 5);
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    sem.release(1);
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 4);
    sem.release(4);
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, multi_waiter_co_release) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(5);
    std::array<tmc::task<void>, 5> tasks;
    for (size_t i = 0; i < 5; ++i) {
      tasks[i] = [](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Sem;
        AA.inc();
      }(sem, aa);
    }
    auto t = tmc::spawn_many(tasks).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 5);
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    co_await sem.co_release();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 4);
    co_await sem.co_release();
    co_await sem.co_release();
    co_await sem.co_release();
    co_await sem.co_release();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, release_wakes_waiters_fifo) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::semaphore sem(0);

    auto make_waiter = [](tmc::semaphore& Sem, size_t Idx) -> tmc::task<size_t> {
      co_await Sem;
      co_return Idx;
    };

    auto w0 = tmc::spawn(make_waiter(sem, 0)).fork();
    co_await tmc::reschedule();
    auto w1 = tmc::spawn(make_waiter(sem, 1)).fork();
    co_await tmc::reschedule();
    auto w2 = tmc::spawn(make_waiter(sem, 2)).fork();
    co_await tmc::reschedule();
    auto w3 = tmc::spawn(make_waiter(sem, 3)).fork();
    co_await tmc::reschedule();
    auto w4 = tmc::spawn(make_waiter(sem, 4)).fork();
    co_await tmc::reschedule();

    sem.release();
    EXPECT_EQ(co_await std::move(w0), 0u);
    sem.release();
    EXPECT_EQ(co_await std::move(w1), 1u);
    sem.release();
    EXPECT_EQ(co_await std::move(w2), 2u);
    sem.release();
    EXPECT_EQ(co_await std::move(w3), 3u);
    sem.release();
    EXPECT_EQ(co_await std::move(w4), 4u);
  }());
}

TEST_F(CATEGORY, co_release_wakes_waiters_fifo) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::semaphore sem(0);

    auto make_waiter = [](tmc::semaphore& Sem, size_t Idx) -> tmc::task<size_t> {
      co_await Sem;
      co_return Idx;
    };

    auto w0 = tmc::spawn(make_waiter(sem, 0)).fork();
    co_await tmc::reschedule();
    auto w1 = tmc::spawn(make_waiter(sem, 1)).fork();
    co_await tmc::reschedule();
    auto w2 = tmc::spawn(make_waiter(sem, 2)).fork();
    co_await tmc::reschedule();
    auto w3 = tmc::spawn(make_waiter(sem, 3)).fork();
    co_await tmc::reschedule();
    auto w4 = tmc::spawn(make_waiter(sem, 4)).fork();
    co_await tmc::reschedule();

    co_await sem.co_release();
    EXPECT_EQ(co_await std::move(w0), 0u);
    co_await sem.co_release();
    EXPECT_EQ(co_await std::move(w1), 1u);
    co_await sem.co_release();
    EXPECT_EQ(co_await std::move(w2), 2u);
    co_await sem.co_release();
    EXPECT_EQ(co_await std::move(w3), 3u);
    co_await sem.co_release();
    EXPECT_EQ(co_await std::move(w4), 4u);
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::semaphore> sem;
    sem.emplace(0);
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Sem;
        AA.inc();
      }(*sem, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(*sem, 1);
    EXPECT_EQ(aa.load(), 0);
    // Destroy sem while the task is still waiting.
    sem.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, move_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::semaphore> sem;
    sem.emplace(1);
    std::optional<tmc::semaphore_scope> scope{co_await sem->acquire_scope()};
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Sem;
        AA.inc();
      }(*sem, aa))
        .fork();
    {
      co_await waiter_count_accessor::wait_for_waiter_count(*sem, 1);
      EXPECT_EQ(aa.load(), 0);
      auto s = *std::move(scope);
      scope.reset(); // should do nothing as the scope has been moved
      // The semaphore is still held (by s), so the waiter must remain
      // suspended.
      EXPECT_EQ(waiter_count_accessor::waiter_count(*sem), 1u);
      EXPECT_EQ(aa.load(), 0);
    }
    co_await aa;
    co_await std::move(t);
  }());
}

// Sem should be usable as a mutex to protect access to a non-atomic
// resource with acquire/release semantics
TEST_F(CATEGORY, access_control) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::semaphore sem(1);

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&sem, &count](int) -> tmc::task<void> {
          return [](tmc::semaphore& Sem, size_t& Count) -> tmc::task<void> {
            co_await Sem;
            ++Count;
            Sem.release();
          }(sem, count);
        }
      ),
      1000
    );
    co_await sem;
    EXPECT_EQ(count, 1000);
  }());
}

TEST_F(CATEGORY, access_control_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::semaphore sem(0);

    auto ts = tmc::spawn_many(
                tmc::iter_adapter(
                  0,
                  [&sem, &count](int) -> tmc::task<void> {
                    return [](tmc::semaphore& Sem, size_t& Count) -> tmc::task<void> {
                      auto s = co_await Sem.acquire_scope();
                      ++Count;
                    }(sem, count);
                  }
                ),
                1000
    )
                .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1000);
    sem.release();
    co_await std::move(ts);
    co_await sem;
    EXPECT_EQ(count, 1000);
  }());
}

TEST_F(CATEGORY, co_release) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);
    {
      co_await sem;
      EXPECT_EQ(sem.count(), 0);
      co_await sem.co_release();
      EXPECT_EQ(sem.count(), 1);
      co_await sem;
      EXPECT_EQ(sem.count(), 0);
    }
    {
      atomic_awaitable<int> aa(1);
      auto t =
        tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Sem;
          AA.inc();
        }(sem, aa))
          .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);
      EXPECT_EQ(sem.count(), 0);
      EXPECT_EQ(aa.load(), 0);
      co_await sem.co_release();
      co_await aa;
      co_await std::move(t);
    }
  }());
}

// The task should not be symmetric transferred as it is scheduled with a
// different priority.
TEST_F(CATEGORY, co_release_no_symmetric) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(1);

    // Run at a lower priority so we can't starve the waiter while spinning.
    co_await tmc::change_priority(1);

    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::current_priority(), 0);
        co_await Sem;
        EXPECT_EQ(tmc::current_priority(), 0);
        AA.inc();
      }(sem, aa))
        .with_priority(0)
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await sem.co_release();
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, co_release_return_value) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);

    auto result = co_await [](tmc::semaphore& Sem) -> tmc::task<int> {
      co_await Sem;
      EXPECT_EQ(Sem.count(), 0);
      co_await Sem.co_release_return(42);
      ADD_FAILURE() << "co_release_return should complete the coroutine";
      co_return -1;
    }(sem);

    EXPECT_EQ(result, 42);
    EXPECT_EQ(sem.count(), 1);
  }());
}

TEST_F(CATEGORY, co_release_return_value_destroys_locals) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);
    std::atomic<size_t> destructor_count{0};
    std::atomic<size_t> parameter_destructor_count{0};

    auto result = co_await
      [](
        tmc::semaphore& Sem, std::atomic<size_t>* DestructorCount,
        std::atomic<size_t>* ParameterDestructorCount, destructor_counter ParameterCounter
      ) -> tmc::task<int> {
      (void)ParameterCounter;
      co_await Sem;
      EXPECT_EQ(Sem.count(), 0);
      destructor_counter counter{DestructorCount};
      EXPECT_EQ(DestructorCount->load(), 0u);
      EXPECT_EQ(ParameterDestructorCount->load(), 0u);
      co_await Sem.co_release_return(42);
      ADD_FAILURE() << "co_release_return should complete the coroutine";
      co_return -1;
    }(sem, &destructor_count, &parameter_destructor_count,
        destructor_counter{&parameter_destructor_count});

    EXPECT_EQ(result, 42);
    EXPECT_EQ(destructor_count.load(), 1u);
    EXPECT_EQ(parameter_destructor_count.load(), 1u);
    EXPECT_EQ(sem.count(), 1);
  }());
}

TEST_F(CATEGORY, co_release_return_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(1);
    bool reached_release = false;

    co_await [](tmc::semaphore& Sem, bool& ReachedRelease) -> tmc::task<void> {
      co_await Sem;
      EXPECT_EQ(Sem.count(), 0);
      ReachedRelease = true;
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem, reached_release);

    EXPECT_EQ(reached_release, true);
    EXPECT_EQ(sem.count(), 1);
  }());
}

// When both the awaiting task and the parent task are eligible for symmetric
// transfer, co_release_return should prefer to symmetric transfer to the
// awaiting task and repost the parent to its executor (although this is not
// directly observable in tests, other than that both complete successfully).
TEST_F(CATEGORY, co_release_return_both_eligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(1);

    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::current_priority(), 0);
        co_await Sem;
        EXPECT_EQ(tmc::current_priority(), 0);
        AA.inc();
      }(sem, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);

    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await [](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem);

    // The resource should have been transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await aa;
    co_await std::move(t);
  }());
}

// When the awaiting task is not eligible for symmetric transfer,
// co_release_return should resume the parent task, and post the awaiting task
// to its executor.
TEST_F(CATEGORY, co_release_return_awaiter_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(1);

    // Run at a lower priority so we can't starve the waiter while spinning.
    co_await tmc::change_priority(1);

    // Ineligible for symmetric transfer due to different priority
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::current_priority(), 0);
        co_await Sem;
        EXPECT_EQ(tmc::current_priority(), 0);
        AA.inc();
      }(sem, aa))
        .with_priority(0)
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);

    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    co_await [](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem);

    // The resource should have been transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    co_await aa;
    co_await std::move(t);
  }());
}

// When the parent is not eligible for symmetric transfer, co_release_return
// should resume the awaiting task, and post parent to its executor.
TEST_F(CATEGORY, co_release_return_parent_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::current_priority(), 0);
        co_await Sem;
        EXPECT_EQ(tmc::current_priority(), 0);
        AA.inc();
      }(sem, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);

    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    // Parent ineligible for symmetric transfer due to different priority
    co_await tmc::spawn([](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem))
      .with_priority(1);

    // The resource should have been transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await aa;
    co_await std::move(t);
  }());
}

// When neither the parent nor the awaiting task is eligible for symmetric
// transfer, co_release_return should return std::noop_coroutine() and both
// tasks should be posted to their executors.
TEST_F(CATEGORY, co_release_return_both_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(1);

    tmc::ex_cpu_st exec;
    exec.init();

    // Ineligible for symmetric transfer due to different executor
    auto t = tmc::spawn(
               [](
                 tmc::semaphore& Sem, atomic_awaitable<int>& AA, tmc::ex_any* Exec
               ) -> tmc::task<void> {
                 EXPECT_EQ(tmc::current_executor(), Exec);
                 EXPECT_EQ(tmc::current_priority(), 0);
                 co_await Sem;
                 EXPECT_EQ(tmc::current_executor(), Exec);
                 EXPECT_EQ(tmc::current_priority(), 0);
                 AA.inc();
               }(sem, aa, exec.type_erased())
    )
               .run_on(exec)
               .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);

    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    // Parent ineligible for symmetric transfer due to different priority
    co_await tmc::spawn([](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem))
      .with_priority(1);

    // The resource should have been transferred to the other task.
    // This should be resumed with the correct executor and priority.
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await aa;
    co_await std::move(t);
  }());
}

// When there is no awaiting task, co_release_return should symmetric transfer
// to parent if eligible.
TEST_F(CATEGORY, co_release_return_no_awaiter_parent_eligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await [](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem);

    // The resource should be available since there was no awaiting task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 1);
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

// When there is no awaiting task, co_release_return should not symmetric
// transfer (repost instead) to parent when it is ineligible.
TEST_F(CATEGORY, co_release_return_no_awaiter_parent_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await tmc::spawn([](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem))
      .with_priority(1);

    // The resource should be available since there was no awaiting task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 1);
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

// When there is no parent task, co_release_return should symmetric transfer to
// the awaiting task when it is eligible.
TEST_F(CATEGORY, co_release_return_no_parent_waiter_eligible) {
  // Use a single-threaded executor to safely force completion of unsynchronized
  // detached task before executor destruction.
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(1);

    // Eligible for symmetric transfer
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::current_priority(), 0);
        co_await Sem;
        EXPECT_EQ(tmc::current_priority(), 0);
        AA.inc();
      }(sem, aa))
        .fork();
    co_await tmc::reschedule();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);

    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    // Detach so there is no parent
    tmc::spawn([](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem))
      .detach();

    co_await tmc::reschedule();

    // The resource should have been transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await aa;
    co_await std::move(t);
  }());
}

// When there is no parent task, co_release_return should not symmetric
// transfer (repost instead) to the awaiting task when it is ineligible.
TEST_F(CATEGORY, co_release_return_no_parent_waiter_ineligible) {
  // Use a single-threaded executor to safely force completion of unsynchronized
  // detached task before executor destruction.
  tmc::ex_cpu_st exec;
  exec.set_priority_count(2).init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    atomic_awaitable<int> aa(1);

    // This needs to become priority 1 so that reschedule() allows the forked
    // task to run. Hacky but this is the best way to force certain sequences
    // (that would normally be multi-threaded / racy) to reliably occur in
    // tests.
    co_await tmc::change_priority(1);

    // Ineligible for symmetric transfer due to different priority
    auto t =
      tmc::spawn([](tmc::semaphore& Sem, atomic_awaitable<int>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::current_priority(), 0);
        co_await Sem;
        EXPECT_EQ(tmc::current_priority(), 0);
        AA.inc();
      }(sem, aa))
        .with_priority(0)
        .fork();
    co_await tmc::reschedule();
    co_await waiter_count_accessor::wait_for_waiter_count(sem, 1);

    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    // Detach so there is no parent
    tmc::spawn([](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem))
      .detach();

    co_await tmc::reschedule();

    // The resource should have been transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 0);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await aa;
    co_await std::move(t);
  }());
}

// When there is no awaiting task or parent, co_release_return should return
// std::noop_coroutine.
TEST_F(CATEGORY, co_release_return_no_awaiter_or_parent) {
  // Use a single-threaded executor to safely force completion of unsynchronized
  // detached task before executor destruction.
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::semaphore sem(0);
    EXPECT_EQ(tmc::current_priority(), 0);

    tmc::spawn([](auto& Sem) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Sem.co_release_return();
      ADD_FAILURE() << "co_release_return should complete the coroutine";
    }(sem))
      .detach();

    co_await tmc::reschedule();

    // The resource should be available since there was no awaiting task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(sem.count(), 1);
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

#undef CATEGORY

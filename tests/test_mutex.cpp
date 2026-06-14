#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/mutex.hpp"
#include "waiter_count_accessor.hpp"

#include <gtest/gtest.h>

#include <array>

#define CATEGORY test_mutex

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }

  using waiter_count_accessor = tmc::tests::waiter_count_accessor;
};

TEST_F(CATEGORY, nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    EXPECT_EQ(mut.is_locked(), false);
    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);
    mut.unlock();
    EXPECT_EQ(mut.is_locked(), false);
    {
      tmc::mutex_scope s{co_await mut.lock_scope()};
      EXPECT_EQ(mut.is_locked(), true);
    }
    EXPECT_EQ(mut.is_locked(), false);
    {
      auto s = co_await mut.lock_scope();
      EXPECT_EQ(mut.is_locked(), true);
    }
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, one_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Mut;
          AA.inc();
        }(mut, aa)
      )
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    mut.unlock();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, multi_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    atomic_awaitable<int> aa(5);
    std::array<tmc::task<void>, 5> tasks;
    for (size_t i = 0; i < 5; ++i) {
      tasks[i] =
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await Mut;
        AA.inc();
        Mut.unlock();
      }(mut, aa);
    }
    auto t = tmc::spawn_many(tasks).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 5);
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    mut.unlock();
    co_await aa;
    EXPECT_EQ(aa.load(), 5);
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, unlock_wakes_waiters_fifo) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;

    auto make_waiter = [](tmc::mutex& Mut, size_t Idx) -> tmc::task<size_t> {
      co_await Mut;
      co_return Idx;
    };

    auto w0 = tmc::spawn(make_waiter(mut, 0)).fork();
    co_await tmc::reschedule();
    auto w1 = tmc::spawn(make_waiter(mut, 1)).fork();
    co_await tmc::reschedule();
    auto w2 = tmc::spawn(make_waiter(mut, 2)).fork();
    co_await tmc::reschedule();
    auto w3 = tmc::spawn(make_waiter(mut, 3)).fork();
    co_await tmc::reschedule();
    auto w4 = tmc::spawn(make_waiter(mut, 4)).fork();
    co_await tmc::reschedule();

    mut.unlock();
    EXPECT_EQ(co_await std::move(w0), 0u);
    mut.unlock();
    EXPECT_EQ(co_await std::move(w1), 1u);
    mut.unlock();
    EXPECT_EQ(co_await std::move(w2), 2u);
    mut.unlock();
    EXPECT_EQ(co_await std::move(w3), 3u);
    mut.unlock();
    EXPECT_EQ(co_await std::move(w4), 4u);
    mut.unlock();
  }());
}

TEST_F(CATEGORY, co_unlock_wakes_waiters_fifo) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;

    auto make_waiter = [](tmc::mutex& Mut, size_t Idx) -> tmc::task<size_t> {
      co_await Mut;
      co_return Idx;
    };

    auto w0 = tmc::spawn(make_waiter(mut, 0)).fork();
    co_await tmc::reschedule();
    auto w1 = tmc::spawn(make_waiter(mut, 1)).fork();
    co_await tmc::reschedule();
    auto w2 = tmc::spawn(make_waiter(mut, 2)).fork();
    co_await tmc::reschedule();
    auto w3 = tmc::spawn(make_waiter(mut, 3)).fork();
    co_await tmc::reschedule();
    auto w4 = tmc::spawn(make_waiter(mut, 4)).fork();
    co_await tmc::reschedule();

    co_await mut.co_unlock();
    EXPECT_EQ(co_await std::move(w0), 0u);
    co_await mut.co_unlock();
    EXPECT_EQ(co_await std::move(w1), 1u);
    co_await mut.co_unlock();
    EXPECT_EQ(co_await std::move(w2), 2u);
    co_await mut.co_unlock();
    EXPECT_EQ(co_await std::move(w3), 3u);
    co_await mut.co_unlock();
    EXPECT_EQ(co_await std::move(w4), 4u);
    co_await mut.co_unlock();
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::mutex> mut;
    mut.emplace();
    co_await *mut;
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Mut;
          AA.inc();
        }(*mut, aa)
      )
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(*mut, 1);
    EXPECT_EQ(aa.load(), 0);
    // Destroy mut while the task is still waiting.
    mut.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, move_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::mutex> mut;
    mut.emplace();
    std::optional<tmc::mutex_scope> scope{co_await mut->lock_scope()};
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          co_await Mut;
          AA.inc();
        }(*mut, aa)
      )
        .fork();
    {
      co_await waiter_count_accessor::wait_for_waiter_count(*mut, 1);
      EXPECT_EQ(aa.load(), 0);
      auto s = *std::move(scope);
      scope.reset(); // should do nothing as the scope has been moved
      // The mutex is still held (by s), so the waiter must remain suspended.
      EXPECT_EQ(waiter_count_accessor::waiter_count(*mut), 1u);
      EXPECT_EQ(aa.load(), 0);
    }
    co_await aa;
    co_await std::move(t);
  }());
}

// Protect access to a non-atomic resource with acquire/release semantics
TEST_F(CATEGORY, access_control) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::mutex mut;

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&mut, &count](int) -> tmc::task<void> {
          return [](tmc::mutex& Mut, size_t& Count) -> tmc::task<void> {
            co_await Mut;
            ++Count;
            Mut.unlock();
          }(mut, count);
        }
      ),
      1000
    );
    co_await mut;
    EXPECT_EQ(count, 1000);
  }());
}

TEST_F(CATEGORY, access_control_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::mutex mut;
    co_await mut;

    auto ts =
      tmc::spawn_many(
        tmc::iter_adapter(
          0,
          [&mut, &count](int) -> tmc::task<void> {
            return [](tmc::mutex& Mut, size_t& Count) -> tmc::task<void> {
              auto s = co_await Mut.lock_scope();
              ++Count;
            }(mut, count);
          }
        ),
        1000
      )
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1000);
    mut.unlock();
    co_await std::move(ts);
    co_await mut;
    EXPECT_EQ(count, 1000);
  }());
}

TEST_F(CATEGORY, co_unlock) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    {
      co_await mut;
      EXPECT_EQ(mut.is_locked(), true);
      co_await mut.co_unlock();
      EXPECT_EQ(mut.is_locked(), false);
      co_await mut;
      EXPECT_EQ(mut.is_locked(), true);
    }
    {
      atomic_awaitable<int> aa(1);
      auto t =
        tmc::spawn(
          [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
            co_await Mut;
            AA.inc();
          }(mut, aa)
        )
          .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);
      EXPECT_EQ(mut.is_locked(), true);
      EXPECT_EQ(aa.load(), 0);
      co_await mut.co_unlock();
      co_await aa;
      co_await std::move(t);
    }
  }());
}

// The task should not be symmetric transferred as it is scheduled with a
// different priority.
TEST_F(CATEGORY, co_unlock_no_symmetric) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 1);
          co_await Mut;
          EXPECT_EQ(tmc::current_priority(), 1);
          AA.inc();
        }(mut, aa)
      )
        .with_priority(1)
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);

    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await mut.co_unlock();

    EXPECT_EQ(tmc::current_priority(), 0);

    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, co_unlock_return_value) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;

    auto result = co_await [](tmc::mutex& Mut) -> tmc::task<int> {
      co_await Mut;
      EXPECT_EQ(Mut.is_locked(), true);
      co_await Mut.co_unlock_return(42);
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
      co_return -1;
    }(mut);

    EXPECT_EQ(result, 42);
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

// ensure that co_unlock_return runs the destructor for locals exactly once, as
// if by the co_return statement
TEST_F(CATEGORY, co_unlock_return_value_destroys_locals) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    std::atomic<size_t> destructor_count{0};
    std::atomic<size_t> parameter_destructor_count{0};

    auto result =
      co_await [](
                 tmc::mutex& Mut, std::atomic<size_t>* DestructorCount,
                 std::atomic<size_t>* ParameterDestructorCount,
                 destructor_counter ParameterCounter
               ) -> tmc::task<int> {
      (void)ParameterCounter;
      co_await Mut;
      EXPECT_EQ(Mut.is_locked(), true);
      destructor_counter counter{DestructorCount};
      EXPECT_EQ(DestructorCount->load(), 0u);
      EXPECT_EQ(ParameterDestructorCount->load(), 0u);
      co_await Mut.co_unlock_return(42);
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
      co_return -1;
    }(mut, &destructor_count, &parameter_destructor_count,
                 destructor_counter{&parameter_destructor_count});

    EXPECT_EQ(result, 42);
    EXPECT_EQ(destructor_count.load(), 1u);
    EXPECT_EQ(parameter_destructor_count.load(), 1u);
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, co_unlock_return_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    bool reached_unlock = false;

    co_await [](tmc::mutex& Mut, bool& ReachedUnlock) -> tmc::task<void> {
      co_await Mut;
      EXPECT_EQ(Mut.is_locked(), true);
      ReachedUnlock = true;
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut, reached_unlock);

    EXPECT_EQ(reached_unlock, true);
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

// When both the awaiting task and the parent task are eligible for symmetric
// transfer, co_unlock_return should prefer to symmetric transfer to the
// awaiting task and repost the parent to its executor (although this is not
// directly observable in tests, other than that both complete successfully).
TEST_F(CATEGORY, co_unlock_return_both_eligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);

    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 0);
          co_await Mut;
          EXPECT_EQ(tmc::current_priority(), 0);
          AA.inc();
        }(mut, aa)
      )
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);

    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await [](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut);

    // The mutex should still be locked, but transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await aa;
    co_await std::move(t);
  }());
}

// When the awaiting task is not eligible for symmetric transfer,
// co_unlock_return should resume the parent task, and post the awaiting task to
// its executor.
TEST_F(CATEGORY, co_unlock_return_awaiter_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);

    // Ineligible for symmetric transfer due to different priority
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 1);
          co_await Mut;
          EXPECT_EQ(tmc::current_priority(), 1);
          AA.inc();
        }(mut, aa)
      )
        .with_priority(1)
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);

    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await [](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut);

    // The mutex should still be locked, but transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await aa;
    co_await std::move(t);
  }());
}

// When the parent is not eligible for symmetric transfer, co_unlock_return
// should resume the awaiting task, and post parent to its executor.
TEST_F(CATEGORY, co_unlock_return_parent_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 0);
          co_await Mut;
          EXPECT_EQ(tmc::current_priority(), 0);
          AA.inc();
        }(mut, aa)
      )
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);

    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    // Parent ineligible for symmetric transfer due to different priority
    co_await tmc::spawn([](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut))
      .with_priority(1);

    // The mutex should still be locked, but transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await aa;
    co_await std::move(t);
  }());
}

// When neither the parent nor the awaiting task is eligible for symmetric
// transfer, co_unlock_return should return std::noop_coroutine() and both tasks
// should be posted to their executors.
TEST_F(CATEGORY, co_unlock_return_both_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);

    tmc::ex_cpu_st exec;
    exec.init();

    // Ineligible for symmetric transfer due to different executor
    auto t = tmc::spawn(
               [](
                 tmc::mutex& Mut, atomic_awaitable<int>& AA, tmc::ex_any* Exec
               ) -> tmc::task<void> {
                 EXPECT_EQ(tmc::current_executor(), Exec);
                 EXPECT_EQ(tmc::current_priority(), 0);
                 co_await Mut;
                 EXPECT_EQ(tmc::current_executor(), Exec);
                 EXPECT_EQ(tmc::current_priority(), 0);
                 AA.inc();
               }(mut, aa, exec.type_erased())
    )
               .run_on(exec)
               .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);

    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    // Parent ineligible for symmetric transfer due to different priority
    co_await tmc::spawn([](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut))
      .with_priority(1);

    // The mutex should still be locked, but transferred to the other task.
    // This should be resumed with the correct executor and priority.
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await aa;
    co_await std::move(t);
  }());
}

// When there is no awaiting task, co_unlock_return should symmetric transfer to
// parent if eligible.
TEST_F(CATEGORY, co_unlock_return_no_awaiter_parent_eligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await [](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut);

    // The mutex should not be locked since there was no awaiting task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), false);
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

// When there is no awaiting task, co_unlock_return should not symmetric
// transfer (repost instead) to parent when it is ineligible.
TEST_F(CATEGORY, co_unlock_return_no_awaiter_parent_ineligible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    EXPECT_EQ(tmc::current_priority(), 0);

    co_await tmc::spawn([](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut))
      .with_priority(1);

    // The mutex should not be locked since there was no awaiting task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), false);
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

// When there is no parent task, co_unlock_return should symmetric transfer to
// the awaiting task when it is eligible.
TEST_F(CATEGORY, co_unlock_return_no_parent_waiter_eligible) {
  // Use a single-threaded executor to safely force completion of unsynchronized
  // detached task before executor destruction.
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);

    // Eligible for symmetric transfer
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 0);
          co_await Mut;
          EXPECT_EQ(tmc::current_priority(), 0);
          AA.inc();
        }(mut, aa)
      )
        .fork();
    co_await tmc::reschedule();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);

    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 0);

    // Detach so there is no parent
    tmc::spawn([](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut))
      .detach();

    co_await tmc::reschedule();

    // The mutex should still be locked, but transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await aa;
    co_await std::move(t);
  }());
}

// When there is no parent task, co_unlock_return should not symmetric transfer
// (repost instead) to the awaiting task when it is ineligible.
TEST_F(CATEGORY, co_unlock_return_no_parent_waiter_ineligible) {
  // Use a single-threaded executor to safely force completion of unsynchronized
  // detached task before executor destruction.
  tmc::ex_cpu_st exec;
  exec.set_priority_count(2).init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    atomic_awaitable<int> aa(1);

    // This needs to become priority 1 so that reschedule() allows the forked
    // task to run. Hacky but this is the best way to force certain sequences
    // (that would normally be multi-threaded / racy) to reliably occur in
    // tests.
    co_await tmc::change_priority(1);

    // Ineligible for symmetric transfer due to different priority
    auto t =
      tmc::spawn(
        [](tmc::mutex& Mut, atomic_awaitable<int>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 0);
          co_await Mut;
          EXPECT_EQ(tmc::current_priority(), 0);
          AA.inc();
        }(mut, aa)
      )
        .with_priority(0)
        .fork();
    co_await tmc::reschedule();
    co_await waiter_count_accessor::wait_for_waiter_count(mut, 1);

    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    // Detach so there is no parent
    tmc::spawn([](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut))
      .detach();

    co_await tmc::reschedule();

    // The mutex should still be locked, but transferred to the other task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), true);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await aa;
    co_await std::move(t);
  }());
}

// When there is no awaiting task or parent, co_unlock_return should return
// std::noop_coroutine.
TEST_F(CATEGORY, co_unlock_return_no_awaiter_or_parent) {
  // Use a single-threaded executor to safely force completion of unsynchronized
  // detached task before executor destruction.
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::mutex mut;
    co_await mut;
    EXPECT_EQ(tmc::current_priority(), 0);

    tmc::spawn([](auto& Mut) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await Mut.co_unlock_return();
      ADD_FAILURE() << "co_unlock_return should complete the coroutine";
    }(mut))
      .detach();

    co_await tmc::reschedule();

    // The mutex should not be locked since there was no awaiting task.
    // This should be resumed with the correct priority.
    EXPECT_EQ(mut.is_locked(), false);
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

#undef CATEGORY

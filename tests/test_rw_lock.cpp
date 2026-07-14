#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/manual_reset_event.hpp"
#include "tmc/rw_lock.hpp"
#include "waiter_count_accessor.hpp"

#include <gtest/gtest.h>

#include <array>
#include <type_traits>
#include <vector>

#define CATEGORY test_rw_lock

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

// Acquires a read lock, then increments AA and releases.
static tmc::task<void> rw_read_and_inc(tmc::rw_lock& RW, atomic_awaitable<int>& AA) {
  co_await RW.lock_read();
  AA.inc();
  RW.unlock_read();
}

// Acquires a read lock, asserts it resumed on ExpectedExec, then increments AA
// and releases.
static tmc::task<void> rw_read_check_exec_and_inc(
  tmc::rw_lock& RW, atomic_awaitable<int>& AA, tmc::ex_any* ExpectedExec
) {
  co_await RW.lock_read();
  EXPECT_EQ(tmc::current_executor(), ExpectedExec);
  AA.inc();
  RW.unlock_read();
}

// Acquires a read lock, asserts it resumed at ExpectedPrio, then increments AA
// and releases.
static tmc::task<void> rw_read_check_prio_and_inc(
  tmc::rw_lock& RW, atomic_awaitable<int>& AA, size_t ExpectedPrio
) {
  co_await RW.lock_read();
  EXPECT_EQ(tmc::current_priority(), ExpectedPrio);
  AA.inc();
  RW.unlock_read();
}

TEST_F(CATEGORY, nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    EXPECT_EQ(rw.is_write_locked(), false);
    EXPECT_EQ(rw.reader_count(), 0u);

    // Multiple read locks can be held simultaneously
    co_await rw.lock_read();
    EXPECT_EQ(rw.reader_count(), 1u);
    co_await rw.lock_read();
    EXPECT_EQ(rw.reader_count(), 2u);
    rw.unlock_read();
    rw.unlock_read();
    EXPECT_EQ(rw.reader_count(), 0u);

    co_await rw.lock_write();
    EXPECT_EQ(rw.is_write_locked(), true);
    rw.unlock_write();
    EXPECT_EQ(rw.is_write_locked(), false);

    {
      tmc::rw_lock_read_scope s{co_await rw.lock_read_scope()};
      EXPECT_EQ(rw.reader_count(), 1u);
    }
    EXPECT_EQ(rw.reader_count(), 0u);
    {
      auto s = co_await rw.lock_write_scope();
      EXPECT_EQ(rw.is_write_locked(), true);
    }
    EXPECT_EQ(rw.is_write_locked(), false);
  }());
}

TEST_F(CATEGORY, try_lock_nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;

    // The write lock can be acquired when the lock is fully idle.
    EXPECT_TRUE(rw.try_lock_write());
    EXPECT_EQ(rw.is_write_locked(), true);
    // While a writer holds the lock, neither a reader nor another writer
    // can acquire it.
    EXPECT_FALSE(rw.try_lock_read());
    EXPECT_FALSE(rw.try_lock_write());
    rw.unlock_write();
    EXPECT_EQ(rw.is_write_locked(), false);

    // Multiple read locks can be acquired simultaneously.
    EXPECT_TRUE(rw.try_lock_read());
    EXPECT_EQ(rw.reader_count(), 1u);
    EXPECT_TRUE(rw.try_lock_read());
    EXPECT_EQ(rw.reader_count(), 2u);
    // A writer cannot acquire while any reader holds the lock.
    EXPECT_FALSE(rw.try_lock_write());
    rw.unlock_read();
    // Still one reader remaining; the writer must still fail.
    EXPECT_FALSE(rw.try_lock_write());
    rw.unlock_read();
    EXPECT_EQ(rw.reader_count(), 0u);

    // Idle again; the writer succeeds.
    EXPECT_TRUE(rw.try_lock_write());
    rw.unlock_write();
    co_return;
  }());
}

// try_lock_read() must fail rather than join the active readers when a writer
// is waiting, matching the phase-fair policy of lock_read().
TEST_F(CATEGORY, try_lock_read_fails_with_waiting_writer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    EXPECT_TRUE(rw.try_lock_read());

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn([](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_write();
        AA.inc();
        RW.unlock_write();
      }(rw, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 1);

    // A new reader must queue behind the waiting writer, so the non-blocking
    // attempt fails and the reader count is unchanged.
    EXPECT_FALSE(rw.try_lock_read());
    EXPECT_EQ(rw.reader_count(), 1u);

    rw.unlock_read();
    co_await aa;
    co_await std::move(t);
  }());
}

// Readers do not block each other; all of the spawned readers can hold the
// lock at the same time as the main task.
TEST_F(CATEGORY, readers_shared) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    co_await rw.lock_read();

    atomic_awaitable<int> aa(5);
    tmc::manual_reset_event release;
    std::array<tmc::task<void>, 5> tasks;
    for (size_t i = 0; i < 5; ++i) {
      tasks[i] = [](
                   tmc::rw_lock& RW, atomic_awaitable<int>& AA,
                   tmc::manual_reset_event& Release
                 ) -> tmc::task<void> {
        co_await RW.lock_read();
        AA.inc();
        // Hold the read lock until all readers have acquired it.
        co_await Release;
        RW.unlock_read();
      }(rw, aa, release);
    }
    auto t = tmc::spawn_many(tasks).fork();
    co_await aa;
    EXPECT_EQ(rw.reader_count(), 6u);
    release.set();
    co_await std::move(t);
    EXPECT_EQ(rw.reader_count(), 1u);
    rw.unlock_read();
  }());
}

TEST_F(CATEGORY, writer_blocks_readers) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    co_await rw.lock_write();

    atomic_awaitable<int> aa(2);
    std::array<tmc::task<void>, 2> tasks;
    for (size_t i = 0; i < 2; ++i) {
      tasks[i] = [](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_read();
        AA.inc();
        RW.unlock_read();
      }(rw, aa);
    }
    auto t = tmc::spawn_many(tasks).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 2);
    EXPECT_EQ(rw.reader_count(), 0u);
    EXPECT_EQ(aa.load(), 0);
    rw.unlock_write();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, readers_block_writer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    co_await rw.lock_read();
    co_await rw.lock_read();

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn([](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_write();
        AA.inc();
        RW.unlock_write();
      }(rw, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 1);
    EXPECT_EQ(aa.load(), 0);

    // The writer must remain suspended while any reader holds the lock.
    rw.unlock_read();
    EXPECT_EQ(waiter_count_accessor::waiter_count(rw), 1u);
    EXPECT_EQ(aa.load(), 0);

    rw.unlock_read();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, writer_blocks_writer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    co_await rw.lock_write();

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn([](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_write();
        AA.inc();
        RW.unlock_write();
      }(rw, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 1);
    EXPECT_EQ(aa.load(), 0);
    rw.unlock_write();
    co_await aa;
    co_await std::move(t);
  }());
}

// A reader that arrives while a writer is waiting must queue behind it
// rather than joining the current group of active readers.
TEST_F(CATEGORY, new_readers_blocked_by_waiting_writer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    co_await rw.lock_read();

    atomic_awaitable<int> writerAA(1);
    atomic_awaitable<int> readerAA(1);
    auto wt = tmc::spawn(
                [](
                  tmc::rw_lock& RW, atomic_awaitable<int>& WriterAA,
                  atomic_awaitable<int>& ReaderAA
                ) -> tmc::task<void> {
                  co_await RW.lock_write();
                  // The reader queued behind this writer; it cannot have run.
                  EXPECT_EQ(ReaderAA.load(), 0);
                  WriterAA.inc();
                  RW.unlock_write();
                }(rw, writerAA, readerAA)
    )
                .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 1);

    auto rt = tmc::spawn(
                [](
                  tmc::rw_lock& RW, atomic_awaitable<int>& WriterAA,
                  atomic_awaitable<int>& ReaderAA
                ) -> tmc::task<void> {
                  co_await RW.lock_read();
                  // The writer must have completed first.
                  EXPECT_EQ(WriterAA.load(), 1);
                  ReaderAA.inc();
                  RW.unlock_read();
                }(rw, writerAA, readerAA)
    )
                .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 2);
    // The new reader suspended instead of joining this task's read lock.
    EXPECT_EQ(rw.reader_count(), 1u);
    EXPECT_EQ(readerAA.load(), 0);

    rw.unlock_read();
    co_await readerAA;
    co_await std::move(wt);
    co_await std::move(rt);
  }());
}

// When a writer releases the lock, all waiting readers are woken as a batch,
// including readers that arrived after a waiting writer. The writer phase
// follows once the readers have drained.
TEST_F(CATEGORY, unlock_write_wakes_reader_batch) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::rw_lock rw;
    co_await rw.lock_write();

    std::vector<size_t> order;

    auto make_reader =
      [](tmc::rw_lock& RW, std::vector<size_t>& Order, size_t Idx) -> tmc::task<void> {
      co_await RW.lock_read();
      Order.push_back(Idx);
      RW.unlock_read();
    };
    auto make_writer =
      [](tmc::rw_lock& RW, std::vector<size_t>& Order, size_t Idx) -> tmc::task<void> {
      co_await RW.lock_write();
      Order.push_back(Idx);
      RW.unlock_write();
    };

    auto r0 = tmc::spawn(make_reader(rw, order, 0)).fork();
    co_await tmc::reschedule();
    auto w1 = tmc::spawn(make_writer(rw, order, 1)).fork();
    co_await tmc::reschedule();
    auto r2 = tmc::spawn(make_reader(rw, order, 2)).fork();
    co_await tmc::reschedule();
    EXPECT_EQ(waiter_count_accessor::waiter_count(rw), 3u);

    rw.unlock_write();
    co_await std::move(r0);
    co_await std::move(w1);
    co_await std::move(r2);

    EXPECT_EQ(order.size(), 3u);
    // Both readers run in the reader phase; the writer goes last.
    EXPECT_EQ(order[2], 1u);
  }());
}

// Protect access to non-atomic resources with acquire/release semantics.
// Readers verify the invariant that writers maintain.
TEST_F(CATEGORY, access_control) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t a = 0;
    size_t b = 0;
    tmc::rw_lock rw;

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&rw, &a, &b](int i) -> tmc::task<void> {
          if (i % 4 == 0) {
            return [](tmc::rw_lock& RW, size_t& A, size_t& B) -> tmc::task<void> {
              co_await RW.lock_write();
              ++A;
              ++B;
              RW.unlock_write();
            }(rw, a, b);
          } else {
            return [](tmc::rw_lock& RW, size_t& A, size_t& B) -> tmc::task<void> {
              co_await RW.lock_read();
              EXPECT_EQ(A, B);
              RW.unlock_read();
            }(rw, a, b);
          }
        }
      ),
      1000
    );
    co_await rw.lock_read();
    EXPECT_EQ(a, 250u);
    EXPECT_EQ(b, 250u);
  }());
}

TEST_F(CATEGORY, access_control_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t a = 0;
    size_t b = 0;
    tmc::rw_lock rw;
    co_await rw.lock_write();

    auto ts =
      tmc::spawn_many(
        tmc::iter_adapter(
          0,
          [&rw, &a, &b](int i) -> tmc::task<void> {
            if (i % 4 == 0) {
              return [](tmc::rw_lock& RW, size_t& A, size_t& B) -> tmc::task<void> {
                auto s = co_await RW.lock_write_scope();
                ++A;
                ++B;
              }(rw, a, b);
            } else {
              return [](tmc::rw_lock& RW, size_t& A, size_t& B) -> tmc::task<void> {
                auto s = co_await RW.lock_read_scope();
                EXPECT_EQ(A, B);
              }(rw, a, b);
            }
          }
        ),
        1000
      )
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 1000);
    rw.unlock_write();
    co_await std::move(ts);
    co_await rw.lock_read();
    EXPECT_EQ(a, 250u);
    EXPECT_EQ(b, 250u);
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(2);
    std::optional<tmc::rw_lock> rw;
    rw.emplace();
    co_await rw->lock_write();
    auto rt =
      tmc::spawn([](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_read();
        AA.inc();
      }(*rw, aa))
        .fork();
    auto wt =
      tmc::spawn([](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_write();
        AA.inc();
      }(*rw, aa))
        .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(*rw, 2);
    EXPECT_EQ(aa.load(), 0);
    // Destroy rw while the tasks are still waiting.
    rw.reset();
    co_await aa;
    co_await std::move(rt);
    co_await std::move(wt);
  }());
}

TEST_F(CATEGORY, move_write_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    tmc::rw_lock rw;
    std::optional<tmc::rw_lock_write_scope> scope{co_await rw.lock_write_scope()};
    auto t =
      tmc::spawn([](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_read();
        AA.inc();
        RW.unlock_read();
      }(rw, aa))
        .fork();
    {
      co_await waiter_count_accessor::wait_for_waiter_count(rw, 1);
      EXPECT_EQ(aa.load(), 0);
      auto s = *std::move(scope);
      scope.reset(); // should do nothing as the scope has been moved
      // The write lock is still held (by s), so the waiter must remain
      // suspended.
      EXPECT_EQ(waiter_count_accessor::waiter_count(rw), 1u);
      EXPECT_EQ(aa.load(), 0);
    }
    co_await aa;
    co_await std::move(t);
  }());
}

// Moving a read scope transfers ownership of the read lock; the moved-from
// scope releases nothing on destruction.
TEST_F(CATEGORY, move_read_scope) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    tmc::rw_lock rw;
    std::optional<tmc::rw_lock_read_scope> scope{co_await rw.lock_read_scope()};
    EXPECT_EQ(rw.reader_count(), 1u);
    // A writer cannot acquire while the read lock is held.
    auto t =
      tmc::spawn([](tmc::rw_lock& RW, atomic_awaitable<int>& AA) -> tmc::task<void> {
        co_await RW.lock_write();
        AA.inc();
        RW.unlock_write();
      }(rw, aa))
        .fork();
    {
      co_await waiter_count_accessor::wait_for_waiter_count(rw, 1);
      EXPECT_EQ(aa.load(), 0);
      auto s = *std::move(scope);
      scope.reset(); // should do nothing as the scope has been moved
      // The read lock is still held (by s), so the writer must remain
      // suspended.
      EXPECT_EQ(waiter_count_accessor::waiter_count(rw), 1u);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(rw.reader_count(), 1u);
    }
    co_await aa;
    co_await std::move(t);
  }());
}

// Best-effort stress test. Hammers a single lock with many concurrent readers
// and writers across multiple worker threads to exercise the lock-free
// wake/handoff race paths in try_wake() (e.g. waking readers from a reader's
// release, or a new reader joining an in-progress batch wake). Those paths only
// occur under specific interleavings that cannot be reproduced deterministically.
TEST_F(CATEGORY, stress) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    size_t a = 0;
    size_t b = 0;
    constexpr int TASK_COUNT = 64;
    constexpr int ITERS = 200;

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&rw, &a, &b](int i) -> tmc::task<void> {
          if (i % 4 == 0) {
            return [](tmc::rw_lock& RW, size_t& A, size_t& B) -> tmc::task<void> {
              for (int j = 0; j < ITERS; ++j) {
                auto s = co_await RW.lock_write_scope();
                ++A;
                ++B;
              }
            }(rw, a, b);
          } else {
            return [](tmc::rw_lock& RW, size_t& A, size_t& B) -> tmc::task<void> {
              for (int j = 0; j < ITERS; ++j) {
                auto s = co_await RW.lock_read_scope();
                EXPECT_EQ(A, B);
              }
            }(rw, a, b);
          }
        }
      ),
      TASK_COUNT
    );

    // i % 4 == 0 for i in [0, TASK_COUNT) yields TASK_COUNT/4 writer tasks,
    // each performing ITERS write-locked increments.
    co_await rw.lock_read();
    EXPECT_EQ(a, static_cast<size_t>((TASK_COUNT / 4) * ITERS));
    EXPECT_EQ(b, static_cast<size_t>((TASK_COUNT / 4) * ITERS));
    rw.unlock_read();
  }());
}

// The rw_lock awaitables are movable so that utility functions (spawn(),
// fork_group, spawn_group, ...) can capture them by value into a wrapper
// task. This makes it safe to pass a temporary and defer the co_await.
static_assert(std::is_move_constructible_v<tmc::aw_rw_lock_read>);
static_assert(!std::is_copy_constructible_v<tmc::aw_rw_lock_read>);
static_assert(std::is_move_constructible_v<tmc::aw_rw_lock_write>);
static_assert(!std::is_copy_constructible_v<tmc::aw_rw_lock_write>);
static_assert(std::is_move_constructible_v<tmc::aw_rw_lock_read_scope>);
static_assert(!std::is_copy_constructible_v<tmc::aw_rw_lock_read_scope>);
static_assert(std::is_move_constructible_v<tmc::aw_rw_lock_write_scope>);
static_assert(!std::is_copy_constructible_v<tmc::aw_rw_lock_write_scope>);

TEST_F(CATEGORY, fork_temporary_lock_scopes) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::rw_lock rw;
    co_await rw.lock_write();
    // The temporaries returned by lock_read_scope() / lock_write_scope() are
    // destroyed at the end of their statements, but the forked wrapper tasks
    // own copies of them, which suspend on the write-locked rw_lock.
    auto tr = tmc::spawn(rw.lock_read_scope()).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 1);
    auto tw = tmc::spawn(rw.lock_write_scope()).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 2);
    rw.unlock_write();
    {
      auto readScope = co_await std::move(tr);
      EXPECT_EQ(rw.reader_count(), 1u);
    }
    EXPECT_EQ(rw.reader_count(), 0u);
    {
      auto writeScope = co_await std::move(tw);
      EXPECT_EQ(rw.is_write_locked(), true);
    }
    EXPECT_EQ(rw.is_write_locked(), false);
  }());
}

// A writer holds the lock while more than one batch worth of readers queue
// behind it (the batch size is 64). Releasing the writer wakes every reader as
// a single bulk operation, posting them to the executor as a full batch
// followed by a partial trailing batch.
TEST_F(CATEGORY, many_reader_waiters) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // 200 spans three full batches (192) plus a partial trailing batch.
    static constexpr size_t COUNT = 200;
    tmc::rw_lock rw;
    co_await rw.lock_write();
    atomic_awaitable<int> aa(COUNT);
    std::vector<tmc::task<void>> tasks(COUNT);
    for (size_t i = 0; i < COUNT; ++i) {
      tasks[i] = rw_read_and_inc(rw, aa);
    }
    auto t = tmc::spawn_many(tasks.data(), COUNT).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, COUNT);
    EXPECT_EQ(aa.load(), 0);
    rw.unlock_write();
    co_await aa;
    EXPECT_EQ(aa.load(), static_cast<int>(COUNT));
    co_await std::move(t);
  }());
}

// Wake readers that resume on different executors. A batch can only be posted
// to a single executor, so a new batch must be started whenever the executor
// changes.
TEST_F(CATEGORY, readers_different_executors) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Enough readers per executor that the interleaving also crosses a batch
    // boundary (64).
    static constexpr size_t PER = 50;
    tmc::rw_lock rw;
    co_await rw.lock_write();
    atomic_awaitable<int> aa(2 * PER);
    std::vector<tmc::task<void>> tasksA(PER);
    std::vector<tmc::task<void>> tasksB(PER);
    for (size_t i = 0; i < PER; ++i) {
      tasksA[i] = rw_read_check_exec_and_inc(rw, aa, tmc::cpu_executor().type_erased());
      tasksB[i] = rw_read_check_exec_and_inc(rw, aa, otherExec.type_erased());
    }
    auto ta = tmc::spawn_many(tasksA.data(), PER).run_on(tmc::cpu_executor()).fork();
    auto tb = tmc::spawn_many(tasksB.data(), PER).run_on(otherExec).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 2 * PER);
    EXPECT_EQ(aa.load(), 0);
    rw.unlock_write();
    co_await aa;
    EXPECT_EQ(aa.load(), static_cast<int>(2 * PER));
    co_await std::move(ta);
    co_await std::move(tb);
  }());
}

// Wake readers that resume at different priorities. A batch can only be posted
// at a single priority, so a new batch must be started whenever the priority
// changes.
TEST_F(CATEGORY, readers_different_priorities) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Enough readers per priority that the interleaving also crosses a batch
    // boundary (64).
    static constexpr size_t PER = 50;
    tmc::rw_lock rw;
    co_await rw.lock_write();
    atomic_awaitable<int> aa(2 * PER);
    std::vector<tmc::task<void>> tasks0(PER);
    std::vector<tmc::task<void>> tasks1(PER);
    for (size_t i = 0; i < PER; ++i) {
      tasks0[i] = rw_read_check_prio_and_inc(rw, aa, 0);
      tasks1[i] = rw_read_check_prio_and_inc(rw, aa, 1);
    }
    auto t0 = tmc::spawn_many(tasks0.data(), PER).with_priority(0).fork();
    auto t1 = tmc::spawn_many(tasks1.data(), PER).with_priority(1).fork();
    co_await waiter_count_accessor::wait_for_waiter_count(rw, 2 * PER);
    EXPECT_EQ(aa.load(), 0);
    rw.unlock_write();
    co_await aa;
    EXPECT_EQ(aa.load(), static_cast<int>(2 * PER));
    co_await std::move(t0);
    co_await std::move(t1);
  }());
}

#undef CATEGORY

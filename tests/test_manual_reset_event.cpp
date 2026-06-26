#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/manual_reset_event.hpp"
#include "waiter_count_accessor.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

#define CATEGORY test_manual_reset_event

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

// Suspends on Event, then increments AA when resumed.
static tmc::task<void>
wait_and_inc(tmc::manual_reset_event& Event, atomic_awaitable<int>& AA) {
  co_await Event;
  AA.inc();
}

// Suspends on Event, then asserts it resumed on ExpectedExec before
// incrementing AA.
static tmc::task<void> wait_check_exec_and_inc(
  tmc::manual_reset_event& Event, atomic_awaitable<int>& AA,
  tmc::ex_any* ExpectedExec
) {
  co_await Event;
  EXPECT_EQ(tmc::current_executor(), ExpectedExec);
  AA.inc();
}

// Suspends on Event, then asserts it resumed at ExpectedPrio before
// incrementing AA.
static tmc::task<void> wait_check_prio_and_inc(
  tmc::manual_reset_event& Event, atomic_awaitable<int>& AA, size_t ExpectedPrio
) {
  co_await Event;
  EXPECT_EQ(tmc::current_priority(), ExpectedPrio);
  AA.inc();
}

TEST_F(CATEGORY, no_waiters) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::manual_reset_event event(false);
    EXPECT_FALSE(event.is_set());
    EXPECT_EQ(event.set(), 0u);
    EXPECT_TRUE(event.is_set());
    co_return;
  }());
}

TEST_F(CATEGORY, no_waiters_co_set) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::manual_reset_event event(false);
    EXPECT_FALSE(event.is_set());
    EXPECT_EQ(co_await event.co_set(), 0u);
    EXPECT_TRUE(event.is_set());
    co_return;
  }());
}

TEST_F(CATEGORY, nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::manual_reset_event event(true);
    EXPECT_EQ(event.is_set(), true);
    co_await event;
    EXPECT_EQ(event.is_set(), true);
    event.reset();
    EXPECT_EQ(event.is_set(), false);
    event.reset();
    EXPECT_EQ(event.is_set(), false);
    EXPECT_EQ(event.set(), 0u);
    EXPECT_EQ(event.is_set(), true);
    EXPECT_EQ(event.set(), 0u);
    EXPECT_EQ(event.is_set(), true);
    EXPECT_EQ(co_await event.co_set(), 0u);
    EXPECT_EQ(event.is_set(), true);
    co_await event;
    EXPECT_EQ(event.is_set(), true);
  }());
}

TEST_F(CATEGORY, one_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::manual_reset_event event;
    EXPECT_EQ(event.is_set(), false);

    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::manual_reset_event& Event, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Event;
                   AA.inc();
                 }(event, aa)
      )
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 1);
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(event.set(), 1u);
      co_await aa;
      co_await std::move(t);
    }
    event.reset();
    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::manual_reset_event& Event, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Event;
                   AA.inc();
                 }(event, aa)
      )
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 1);
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(co_await event.co_set(), 1u);
      co_await aa;
      co_await std::move(t);
    }
  }());
}

TEST_F(CATEGORY, multi_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::manual_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    {
      atomic_awaitable<int> aa(5);
      std::array<tmc::task<void>, 5> tasks;
      for (size_t i = 0; i < 5; ++i) {
        tasks[i] = [](
                     tmc::manual_reset_event& Event, atomic_awaitable<int>& AA
                   ) -> tmc::task<void> {
          co_await Event;
          AA.inc();
        }(event, aa);
      }
      auto t = tmc::spawn_many(tasks).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 5);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(event.set(), 5u);
      co_await waiter_count_accessor::wait_for_waiter_count(event, 0);
      co_await aa;
      EXPECT_EQ(aa.load(), 5);
      co_await std::move(t);
    }
    event.reset();
    {
      atomic_awaitable<int> aa(5);
      std::array<tmc::task<void>, 5> tasks;
      for (size_t i = 0; i < 5; ++i) {
        tasks[i] = [](
                     tmc::manual_reset_event& Event, atomic_awaitable<int>& AA
                   ) -> tmc::task<void> {
          co_await Event;
          AA.inc();
        }(event, aa);
      }
      auto t = tmc::spawn_many(tasks).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 5);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(co_await event.co_set(), 5u);
      co_await waiter_count_accessor::wait_for_waiter_count(event, 0);
      co_await aa;
      EXPECT_EQ(aa.load(), 5);
      co_await std::move(t);
    }
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::manual_reset_event> event;
    event.emplace();
    EXPECT_EQ(event->is_set(), false);
    auto t = tmc::spawn(
               [](
                 tmc::manual_reset_event& Event, atomic_awaitable<int>& AA
               ) -> tmc::task<void> {
                 co_await Event;
                 AA.inc();
               }(*event, aa)
    )
               .fork();
    co_await waiter_count_accessor::wait_for_waiter_count(*event, 1);
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(event->is_set(), false);
    // Destroy event while the task is still waiting.
    // resets the optional, not the wrapped event. unfortunate name collision
    event.reset();
    co_await aa;
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, co_set) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::manual_reset_event event;
    {
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(co_await event.co_set(), 0u);
      EXPECT_EQ(event.is_set(), true);
      event.reset();
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(co_await event.co_set(), 0u);
      EXPECT_EQ(event.is_set(), true);
    }
    event.reset();
    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::manual_reset_event& Event, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Event;
                   AA.inc();
                 }(event, aa)
      )
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 1);
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(co_await event.co_set(), 1u);
      co_await aa;
      co_await std::move(t);
    }
  }());
}

// The task should not be symmetric transferred as it is scheduled with a
// different priority.
TEST_F(CATEGORY, co_set_no_symmetric) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::manual_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::manual_reset_event& Event, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   EXPECT_EQ(tmc::current_priority(), 1);
                   co_await Event;
                   EXPECT_EQ(tmc::current_priority(), 1);
                   AA.inc();
                 }(event, aa)
      )
                 .with_priority(1)
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 1);
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await event.co_set();
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await aa;
      co_await std::move(t);
    }
  }());
}

// Wake more than one batch worth of waiters (the batch size is 64). This
// exercises posting a full batch and then a partial trailing batch.
TEST_F(CATEGORY, many_waiters) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // 200 spans three full batches (192) plus a partial trailing batch.
    static constexpr size_t COUNT = 200;
    tmc::manual_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    {
      atomic_awaitable<int> aa(COUNT);
      std::vector<tmc::task<void>> tasks(COUNT);
      for (size_t i = 0; i < COUNT; ++i) {
        tasks[i] = wait_and_inc(event, aa);
      }
      auto t = tmc::spawn_many(tasks.data(), COUNT).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, COUNT);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(event.set(), COUNT);
      co_await aa;
      EXPECT_EQ(aa.load(), static_cast<int>(COUNT));
      co_await std::move(t);
    }
    event.reset();
    // Repeat via co_set(), which wakes the most-recently-added waiter by
    // symmetric transfer and posts the remainder in batches.
    {
      atomic_awaitable<int> aa(COUNT);
      std::vector<tmc::task<void>> tasks(COUNT);
      for (size_t i = 0; i < COUNT; ++i) {
        tasks[i] = wait_and_inc(event, aa);
      }
      auto t = tmc::spawn_many(tasks.data(), COUNT).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, COUNT);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(co_await event.co_set(), COUNT);
      co_await aa;
      EXPECT_EQ(aa.load(), static_cast<int>(COUNT));
      co_await std::move(t);
    }
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
    tmc::manual_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    {
      atomic_awaitable<int> aa(2 * PER);
      std::vector<tmc::task<void>> tasksA(PER);
      std::vector<tmc::task<void>> tasksB(PER);
      for (size_t i = 0; i < PER; ++i) {
        tasksA[i] =
          wait_check_exec_and_inc(event, aa, tmc::cpu_executor().type_erased());
        tasksB[i] = wait_check_exec_and_inc(event, aa, otherExec.type_erased());
      }
      auto ta =
        tmc::spawn_many(tasksA.data(), PER).run_on(tmc::cpu_executor()).fork();
      auto tb = tmc::spawn_many(tasksB.data(), PER).run_on(otherExec).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 2 * PER);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(event.set(), 2 * PER);
      co_await aa;
      EXPECT_EQ(aa.load(), static_cast<int>(2 * PER));
      co_await std::move(ta);
      co_await std::move(tb);
    }
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
    tmc::manual_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    {
      atomic_awaitable<int> aa(2 * PER);
      std::vector<tmc::task<void>> tasks0(PER);
      std::vector<tmc::task<void>> tasks1(PER);
      for (size_t i = 0; i < PER; ++i) {
        tasks0[i] = wait_check_prio_and_inc(event, aa, 0);
        tasks1[i] = wait_check_prio_and_inc(event, aa, 1);
      }
      auto t0 = tmc::spawn_many(tasks0.data(), PER).with_priority(0).fork();
      auto t1 = tmc::spawn_many(tasks1.data(), PER).with_priority(1).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 2 * PER);
      EXPECT_EQ(aa.load(), 0);
      EXPECT_EQ(event.set(), 2 * PER);
      co_await aa;
      EXPECT_EQ(aa.load(), static_cast<int>(2 * PER));
      co_await std::move(t0);
      co_await std::move(t1);
    }
  }());
}

#undef CATEGORY

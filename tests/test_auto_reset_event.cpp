#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/auto_reset_event.hpp"
#include "tmc/current.hpp"
#include "waiter_count_accessor.hpp"

#include <gtest/gtest.h>

#include <array>

#define CATEGORY test_auto_reset_event

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }

  using waiter_count_accessor = tmc::tests::waiter_count_accessor;
};

TEST_F(CATEGORY, no_waiters) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event(false);
    EXPECT_FALSE(event.is_set());
    event.set();
    EXPECT_TRUE(event.is_set());
    co_return;
  }());
}

TEST_F(CATEGORY, no_waiters_co_set) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event(false);
    EXPECT_FALSE(event.is_set());
    co_await event.co_set();
    EXPECT_TRUE(event.is_set());
    co_return;
  }());
}

TEST_F(CATEGORY, nonblocking) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event(true);
    EXPECT_EQ(event.is_set(), true);
    co_await event;
    EXPECT_EQ(event.is_set(), false);
    event.set();
    EXPECT_EQ(event.is_set(), true);
    event.reset();
    EXPECT_EQ(event.is_set(), false);
    event.reset();
    EXPECT_EQ(event.is_set(), false);
    event.set();
    EXPECT_EQ(event.is_set(), true);
    event.set();
    EXPECT_EQ(event.is_set(), true);
    co_await event;
    EXPECT_EQ(event.is_set(), false);
    co_await event.co_set();
    EXPECT_EQ(event.is_set(), true);
    co_await event.co_set();
    EXPECT_EQ(event.is_set(), true);
    co_await event;
    EXPECT_EQ(event.is_set(), false);
  }());
}

TEST_F(CATEGORY, one_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event;
    EXPECT_EQ(event.is_set(), false);

    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::auto_reset_event& Event, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Event;
                   AA.inc();
                 }(event, aa)
      )
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 1);
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(aa.load(), 0);
      event.set();
      co_await aa;
      co_await std::move(t);
      EXPECT_EQ(event.is_set(), false);
    }
    event.reset();
    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::auto_reset_event& Event, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Event;
                   AA.inc();
                 }(event, aa)
      )
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 1);
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(aa.load(), 0);
      co_await event.co_set();
      co_await aa;
      co_await std::move(t);
      EXPECT_EQ(event.is_set(), false);
    }
  }());
}

TEST_F(CATEGORY, multi_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    {
      atomic_awaitable<int> aa(5);
      std::array<tmc::task<void>, 5> tasks;
      for (size_t i = 0; i < 5; ++i) {
        tasks[i] = [](
                     tmc::auto_reset_event& Event, atomic_awaitable<int>& AA
                   ) -> tmc::task<void> {
          co_await Event;
          AA.inc();
        }(event, aa);
      }
      auto t = tmc::spawn_many(tasks).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 5);
      EXPECT_EQ(aa.load(), 0);
      event.set();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 4);
      event.set();
      event.set();
      event.set();
      event.set();
      co_await aa;
      EXPECT_EQ(aa.load(), 5);
      co_await std::move(t);
    }
  }());
}

TEST_F(CATEGORY, multi_waiter_co_set) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    {
      atomic_awaitable<int> aa(5);
      std::array<tmc::task<void>, 5> tasks;
      for (size_t i = 0; i < 5; ++i) {
        tasks[i] = [](
                     tmc::auto_reset_event& Event, atomic_awaitable<int>& AA
                   ) -> tmc::task<void> {
          co_await Event;
          AA.inc();
        }(event, aa);
      }
      auto t = tmc::spawn_many(tasks).fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 5);
      EXPECT_EQ(aa.load(), 0);
      co_await event.co_set();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 4);
      co_await event.co_set();
      co_await event.co_set();
      co_await event.co_set();
      co_await event.co_set();
      co_await aa;
      EXPECT_EQ(aa.load(), 5);
      co_await std::move(t);
    }
  }());
}

TEST_F(CATEGORY, set_wakes_waiters_fifo) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::auto_reset_event event;

    auto make_waiter = [](tmc::auto_reset_event& Event, size_t Idx)
      -> tmc::task<size_t> {
      co_await Event;
      co_return Idx;
    };

    auto w0 = tmc::spawn(make_waiter(event, 0)).fork();
    co_await tmc::reschedule();
    auto w1 = tmc::spawn(make_waiter(event, 1)).fork();
    co_await tmc::reschedule();
    auto w2 = tmc::spawn(make_waiter(event, 2)).fork();
    co_await tmc::reschedule();
    auto w3 = tmc::spawn(make_waiter(event, 3)).fork();
    co_await tmc::reschedule();
    auto w4 = tmc::spawn(make_waiter(event, 4)).fork();
    co_await tmc::reschedule();

    event.set();
    EXPECT_EQ(co_await std::move(w0), 0u);
    event.set();
    EXPECT_EQ(co_await std::move(w1), 1u);
    event.set();
    EXPECT_EQ(co_await std::move(w2), 2u);
    event.set();
    EXPECT_EQ(co_await std::move(w3), 3u);
    event.set();
    EXPECT_EQ(co_await std::move(w4), 4u);
  }());
}

TEST_F(CATEGORY, co_set_wakes_waiters_fifo) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    tmc::auto_reset_event event;

    auto make_waiter = [](tmc::auto_reset_event& Event, size_t Idx)
      -> tmc::task<size_t> {
      co_await Event;
      co_return Idx;
    };

    auto w0 = tmc::spawn(make_waiter(event, 0)).fork();
    co_await tmc::reschedule();
    auto w1 = tmc::spawn(make_waiter(event, 1)).fork();
    co_await tmc::reschedule();
    auto w2 = tmc::spawn(make_waiter(event, 2)).fork();
    co_await tmc::reschedule();
    auto w3 = tmc::spawn(make_waiter(event, 3)).fork();
    co_await tmc::reschedule();
    auto w4 = tmc::spawn(make_waiter(event, 4)).fork();
    co_await tmc::reschedule();

    co_await event.co_set();
    EXPECT_EQ(co_await std::move(w0), 0u);
    co_await event.co_set();
    EXPECT_EQ(co_await std::move(w1), 1u);
    co_await event.co_set();
    EXPECT_EQ(co_await std::move(w2), 2u);
    co_await event.co_set();
    EXPECT_EQ(co_await std::move(w3), 3u);
    co_await event.co_set();
    EXPECT_EQ(co_await std::move(w4), 4u);
  }());
}

TEST_F(CATEGORY, resume_in_destructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    std::optional<tmc::auto_reset_event> event;
    event.emplace();
    EXPECT_EQ(event->is_set(), false);
    auto t = tmc::spawn(
               [](
                 tmc::auto_reset_event& Event, atomic_awaitable<int>& AA
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

// Event should be usable as a mutex to protect access to a non-atomic
// resource with acquire/release semantics
TEST_F(CATEGORY, access_control) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::auto_reset_event event(1);

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&event, &count](int) -> tmc::task<void> {
          return
            [](tmc::auto_reset_event& Event, size_t& Count) -> tmc::task<void> {
              co_await Event;
              ++Count;
              Event.set();
            }(event, count);
        }
      ),
      1000
    );
    co_await event;
    EXPECT_EQ(count, 1000);
  }());
}

TEST_F(CATEGORY, co_set) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event;
    {
      EXPECT_EQ(event.is_set(), false);
      co_await event.co_set();
      EXPECT_EQ(event.is_set(), true);
      co_await event;
      EXPECT_EQ(event.is_set(), false);
      co_await event.co_set();
      EXPECT_EQ(event.is_set(), true);
    }
    event.reset();
    {
      atomic_awaitable<int> aa(1);
      auto t = tmc::spawn(
                 [](
                   tmc::auto_reset_event& Event, atomic_awaitable<int>& AA
                 ) -> tmc::task<void> {
                   co_await Event;
                   AA.inc();
                 }(event, aa)
      )
                 .fork();
      co_await waiter_count_accessor::wait_for_waiter_count(event, 1);
      EXPECT_EQ(event.is_set(), false);
      EXPECT_EQ(aa.load(), 0);
      co_await event.co_set();
      co_await aa;
      co_await std::move(t);
      EXPECT_EQ(event.is_set(), false);
    }
  }());
}

// The task should not be symmetric transferred as it is scheduled with a
// different priority.
TEST_F(CATEGORY, co_set_no_symmetric) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::auto_reset_event event;
    EXPECT_EQ(event.is_set(), false);
    atomic_awaitable<int> aa(1);
    auto t = tmc::spawn(
               [](
                 tmc::auto_reset_event& Event, atomic_awaitable<int>& AA
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
    EXPECT_EQ(event.is_set(), false);
  }());
}

#undef CATEGORY

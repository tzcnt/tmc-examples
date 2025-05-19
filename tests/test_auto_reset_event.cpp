#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/auto_reset_event.hpp"
#include "tmc/current.hpp"

#include <gtest/gtest.h>

#include <array>
#include <thread>

#define CATEGORY test_auto_reset_event

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

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
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
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
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(aa.load(), 0);
      event.set();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(aa.load(), 1);
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
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(aa.load(), 0);
      co_await event.co_set();
      EXPECT_EQ(aa.load(), 1);
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
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 0);
    EXPECT_EQ(event->is_set(), false);
    // Destroy event while the task is still waiting.
    // resets the optional, not the wrapped event. unfortunate name collision
    event.reset();
    co_await aa;
    co_await std::move(t);
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
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
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

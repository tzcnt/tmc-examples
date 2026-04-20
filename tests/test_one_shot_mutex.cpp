#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/one_shot_mutex.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#define CATEGORY test_one_shot_mutex

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static tmc::task<void> one_shot_mutex_waiter(
  tmc::one_shot_mutex& Mut, atomic_awaitable<int>& WaiterRan,
  tmc::ex_any* ExpectedExecutor
) {
  auto* waiterExecutor = tmc::current_executor();
  auto waiterPriority = tmc::current_priority();
  EXPECT_EQ(waiterExecutor, ExpectedExecutor);
  EXPECT_EQ(waiterPriority, 1);

  co_await Mut;

  WaiterRan.inc();
  EXPECT_EQ(tmc::current_priority(), waiterPriority);
}

TEST_F(CATEGORY, nonblocking_and_co_unlock) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;
    EXPECT_EQ(mut.is_locked(), false);

    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    co_await mut.co_unlock();
    EXPECT_EQ(mut.is_locked(), false);

    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    co_await mut.co_unlock();
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, access_control) {
  test_async_main(ex(), []() -> tmc::task<void> {
    size_t count = 0;
    tmc::one_shot_mutex mut;

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [&mut, &count](int) -> tmc::task<void> {
          return [](tmc::one_shot_mutex& Mut, size_t& Count)
              -> tmc::task<void> {
            co_await Mut;
            ++Count;
          }(mut, count);
        }
      ),
      1000
    );

    EXPECT_EQ(count, 1000);
    co_await tmc::yield();
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, co_unlock_releases_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;

    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    atomic_awaitable<int> aa(1);
    auto t =
      tmc::spawn(
        [](tmc::one_shot_mutex& Mut, atomic_awaitable<int>& AA)
            -> tmc::task<void> {
          co_await Mut;
          AA.inc();
        }(mut, aa)
      )
        .fork();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(aa.load(), 0);

    co_await mut.co_unlock();

    co_await aa;
    EXPECT_EQ(mut.is_locked(), false);
    co_await std::move(t);
  }());
}

TEST_F(CATEGORY, suspension_releases_and_restores_context) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;
    atomic_awaitable<int> waiterRan(1);
    atomic_awaitable<int> release(1);

    std::thread releaser([&release]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      release.inc();
    });

    co_await mut;
    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    EXPECT_EQ(tmc::current_priority(), 0);

    auto waiter =
      tmc::spawn(one_shot_mutex_waiter(mut, waiterRan, ex().type_erased()))
        .with_priority(1)
        .fork();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    EXPECT_EQ(waiterRan.load(), 0);

    co_await release;

    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await std::move(waiter);
    co_await tmc::yield();
    EXPECT_EQ(mut.is_locked(), false);
    releaser.join();
  }());
}

TEST_F(CATEGORY, destroy_local_mutex_while_runner_unwinds) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await []() -> tmc::task<void> {
      tmc::one_shot_mutex mut;
      co_await mut;
    }();
    co_await tmc::yield();
  }());
}

#undef CATEGORY

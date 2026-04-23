#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/one_shot_mutex.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
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

static tmc::task<void> one_shot_mutex_simple_waiter(
  tmc::one_shot_mutex& Mut, atomic_awaitable<int>& WaiterRan
) {
  co_await Mut;
  WaiterRan.inc();
}

static tmc::task<std::unique_ptr<int>>
one_shot_mutex_return_unique_ptr(tmc::one_shot_mutex& Mut, int Value) {
  co_await Mut;
  co_await Mut.co_unlock_return_value(std::make_unique<int>(Value));
  co_return nullptr;
}

static tmc::task<void> one_shot_mutex_return_void(tmc::one_shot_mutex& Mut) {
  co_await Mut;
  co_await Mut.co_unlock_return_void();
  co_return;
}

static tmc::task<void> one_shot_mutex_return_void_with_destructor(
  tmc::one_shot_mutex& Mut, std::atomic<size_t>& DestroyCount
) {
  co_await Mut;
  destructor_counter counter(&DestroyCount);
  co_await Mut.co_unlock_return_void();
}

static tmc::task<void> one_shot_mutex_unlock_then_return_void_with_destructor(
  tmc::one_shot_mutex& Mut, std::atomic<size_t>& DestroyCount
) {
  co_await Mut;
  destructor_counter counter(&DestroyCount);
  co_await Mut.co_unlock();
  co_return;
}

static tmc::task<void> one_shot_mutex_observe_destroy_count(
  tmc::one_shot_mutex& Mut, atomic_awaitable<int>& WaiterRan,
  std::atomic<size_t>& DestroyCount, size_t& ObservedDestroyCount
) {
  co_await Mut;
  ObservedDestroyCount = DestroyCount.load(std::memory_order_acquire);
  WaiterRan.inc();
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
          return
            [](tmc::one_shot_mutex& Mut, size_t& Count) -> tmc::task<void> {
              co_await Mut;
              ++Count;
            }(mut, count);
        }
      ),
      1000
    );

    EXPECT_EQ(count, 1000);
    co_await tmc::yield();
    // mutex may not be immediately unlocked while the runner is executing, but
    // we should be able to get the lock now
    co_await mut;
  }());
}

TEST_F(CATEGORY, co_unlock_releases_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;

    co_await mut;
    EXPECT_EQ(mut.is_locked(), true);

    atomic_awaitable<int> aa(1);
    auto t = tmc::spawn(
               [](
                 tmc::one_shot_mutex& Mut, atomic_awaitable<int>& AA
               ) -> tmc::task<void> {
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

TEST_F(CATEGORY, co_unlock_return_value_returns_move_only_result) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;
    atomic_awaitable<int> waiterRan(1);

    co_await mut;

    auto waiter =
      tmc::spawn(one_shot_mutex_simple_waiter(mut, waiterRan)).fork();
    auto child = tmc::spawn(one_shot_mutex_return_unique_ptr(mut, 42)).fork();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    co_await mut.co_unlock();

    auto result = co_await std::move(child);
    EXPECT_NE(result, nullptr);
    EXPECT_EQ(*result, 42);

    co_await waiterRan;
    co_await std::move(waiter);
    co_await tmc::yield();
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, co_unlock_return_void_releases_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;
    atomic_awaitable<int> waiterRan(1);

    co_await mut;

    auto waiter =
      tmc::spawn(one_shot_mutex_simple_waiter(mut, waiterRan)).fork();
    auto child = tmc::spawn(one_shot_mutex_return_void(mut)).fork();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    co_await mut.co_unlock();

    co_await std::move(child);
    co_await waiterRan;
    co_await std::move(waiter);
    co_await tmc::yield();
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, co_unlock_return_void_destroys_locals_before_next_waiter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;
    atomic_awaitable<int> waiterRan(1);
    std::atomic<size_t> destroyCount = 0;
    size_t observedDestroyCount = 0;

    co_await mut;

    auto waiter =
      tmc::spawn(one_shot_mutex_observe_destroy_count(
                   mut, waiterRan, destroyCount, observedDestroyCount
                 ))
        .fork();
    auto child =
      tmc::spawn(one_shot_mutex_return_void_with_destructor(mut, destroyCount))
        .fork();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    co_await mut.co_unlock();

    co_await std::move(child);
    co_await waiterRan;
    EXPECT_EQ(observedDestroyCount, 1);

    co_await std::move(waiter);
  }());
}

TEST_F(CATEGORY, co_unlock_then_return_keeps_locals_alive_until_later_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;
    atomic_awaitable<int> waiterRan(1);
    std::atomic<size_t> destroyCount = 0;
    size_t observedDestroyCount = 99;

    co_await mut;

    auto waiter =
      tmc::spawn(one_shot_mutex_observe_destroy_count(
                   mut, waiterRan, destroyCount, observedDestroyCount
                 ))
        .fork();
    auto child =
      tmc::spawn(one_shot_mutex_unlock_then_return_void_with_destructor(
                   mut, destroyCount
                 ))
        .fork();

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    co_await mut.co_unlock();

    co_await waiterRan;
    EXPECT_EQ(observedDestroyCount, 0);
    EXPECT_EQ(destroyCount.load(std::memory_order_acquire), 0);

    co_await std::move(waiter);
    co_await std::move(child);
    EXPECT_EQ(destroyCount.load(std::memory_order_acquire), 1);
  }());
}

TEST_F(CATEGORY, nested_await_keeps_lock_in_outer_coroutine) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;

    co_await [](tmc::one_shot_mutex& OuterMut) -> tmc::task<void> {
      co_await [](tmc::one_shot_mutex& InnerMut) -> tmc::task<void> {
        co_await InnerMut;
      }(OuterMut);

      EXPECT_EQ(OuterMut.is_locked(), true);
    }(mut);

    co_await tmc::yield();
    EXPECT_EQ(mut.is_locked(), false);
  }());
}

TEST_F(CATEGORY, nested_await_releases_lock_when_inner_coroutine_yields) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::one_shot_mutex mut;

    co_await [](tmc::one_shot_mutex& OuterMut) -> tmc::task<void> {
      co_await [](tmc::one_shot_mutex& InnerMut) -> tmc::task<void> {
        co_await InnerMut;
        co_await tmc::yield();
      }(OuterMut);

      EXPECT_EQ(OuterMut.is_locked(), false);
    }(mut);

    EXPECT_EQ(mut.is_locked(), false);
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

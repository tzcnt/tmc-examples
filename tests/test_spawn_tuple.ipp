#include "test_common.hpp"
#include "test_spawn_many_common.hpp"

#include <gtest/gtest.h>

// tests ported from examples/spawn_iterator.cpp

TEST_F(CATEGORY, spawn_tuple_task_detach) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> counter(2);

    auto ts = tmc::spawn_tuple(
      [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        Counter.notify_all();
        co_return;
      }(counter),
      [](std::atomic<int>& Counter) -> tmc::task<void> {
        ++Counter;
        Counter.notify_all();
        co_return;
      }(counter)
    );
    ts.detach();
    co_await counter;
    EXPECT_EQ(counter.load(), 2);
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_each) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto ts = tmc::spawn_tuple(work(0), work(1), work(2));
    auto each = std::move(ts).result_each();

    int sum = 0;
    for (size_t i = co_await each; i != each.end(); i = co_await each) {
      switch (i) {
      case 0:
        sum += each.get<0>();
        break;
      case 1:
        sum += each.get<1>();
        break;
      case 2:
        sum += each.get<2>();
        break;
      default:
        ADD_FAILURE() << "invalid index: " << i;
        break;
      }
    }

    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

TEST_F(CATEGORY, spawn_tuple_each_resume_after) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto make_task = [](int I, atomic_awaitable<size_t>& AA) -> tmc::task<int> {
      AA.inc();
      co_return 1 << I;
    };
    static constexpr int N = 5;
    atomic_awaitable<size_t> aa(N);
    auto ts = tmc::spawn_tuple(
                make_task(0, aa), make_task(1, aa), make_task(2, aa),
                make_task(3, aa), make_task(4, aa)
    )
                .result_each();
    co_await aa;
    int sum = 0;
    for (auto idx = co_await ts; idx != ts.end(); idx = co_await ts) {
      switch (idx) {
      case 0:
        sum += ts.get<0>();
        break;
      case 1:
        sum += ts.get<1>();
        break;
      case 2:
        sum += ts.get<2>();
        break;
      case 3:
        sum += ts.get<3>();
        break;
      case 4:
        sum += ts.get<4>();
        break;
      }
    }

    EXPECT_EQ(sum, (1 << N) - 1);

    co_return;
  }());
}

TEST_F(CATEGORY, spawn_tuple_empty) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::tuple<> results = co_await tmc::spawn_tuple();
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_func) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::tuple<int, int, int> results =
      co_await tmc::spawn_tuple(work(0), work(1), work(2));

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_lambda) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      // non-capturing lambda coroutine
      auto f = [](int i) -> tmc::task<int> { co_return 1 << i; };
      std::tuple<int, int, int> results =
        co_await tmc::spawn_tuple(f(0), f(1), f(2));

      [[maybe_unused]] auto sum =
        std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

      EXPECT_EQ(sum, (1 << 3) - 1);
    }
    {
      // capturing lambda that forwards to non-capturing lambda coroutine
      int i = 0;
      auto f = [&i]() -> tmc::task<int> {
        return [](int j) -> tmc::task<int> { co_return 1 << j; }(i++);
      };
      std::tuple<int, int, int> results =
        co_await tmc::spawn_tuple(f(), f(), f());

      [[maybe_unused]] auto sum =
        std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

      EXPECT_EQ(sum, (1 << 3) - 1);
    }
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_tuple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::tuple<tmc::task<int>, tmc::task<int>, tmc::task<int>> tasks{
      work(0), work(1), work(2)
    };

    std::tuple<int, int, int> results =
      co_await tmc::spawn_tuple(std::move(tasks));

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

TEST_F(CATEGORY, spawn_tuple_task_fork) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto ts = tmc::spawn_tuple(work(0), work(1), work(2));
    auto early = std::move(ts).fork();

    std::tuple<int, int, int> results = co_await std::move(early);

    [[maybe_unused]] auto sum =
      std::get<0>(results) + std::get<1>(results) + std::get<2>(results);

    EXPECT_EQ(sum, (1 << 3) - 1);
  }());
}

TEST_F(CATEGORY, spawn_tuple_from_tuple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::tuple<tmc::task<int>, tmc::task<int>> t{work(0), work(1)};

    std::tuple<int, int> results = co_await tmc::spawn_tuple(std::move(t));

    [[maybe_unused]] auto sum = std::get<0>(results) + std::get<1>(results);

    EXPECT_EQ(sum, (1 << 2) - 1);
  }());
}

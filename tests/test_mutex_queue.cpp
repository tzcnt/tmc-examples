#include "test_common.hpp"
#include "tmc/mutex_queue.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>

#define CATEGORY test_mutex_queue

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, push_pop_front_back) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex_queue<int> queue;

    EXPECT_EQ(co_await queue.try_pop_front(), std::nullopt);
    EXPECT_EQ(co_await queue.try_pop_back(), std::nullopt);

    co_await queue.push_back(1);
    co_await queue.push_back(2);
    co_await queue.push_front(0);
    co_await queue.push_front(-1);

    EXPECT_EQ(co_await queue.try_pop_front(), -1);
    EXPECT_EQ(co_await queue.try_pop_back(), 2);
    EXPECT_EQ(co_await queue.try_pop_front(), 0);
    EXPECT_EQ(co_await queue.try_pop_back(), 1);
    EXPECT_EQ(co_await queue.try_pop_front(), std::nullopt);
  }());
}

TEST_F(CATEGORY, concurrent_pushes_are_serialized) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t Count = 1000;
    tmc::mutex_queue<size_t> queue;

    co_await tmc::spawn_many(
      tmc::iter_adapter(
        size_t{0},
        [&queue](size_t i) -> tmc::task<void> {
          return
            [](tmc::mutex_queue<size_t>* Queue, size_t I) -> tmc::task<void> {
              co_await Queue->push_back(I);
            }(&queue, i);
        }
      ),
      Count
    );

    std::array<bool, Count> seen{};
    for (size_t i = 0; i < Count; ++i) {
      std::optional<size_t> value = co_await queue.try_pop_front();
      EXPECT_TRUE(value.has_value());
      if (!value.has_value()) {
        co_return;
      }
      EXPECT_LT(*value, Count);
      if (*value >= Count) {
        co_return;
      }
      EXPECT_FALSE(seen[*value]);
      seen[*value] = true;
    }

    EXPECT_EQ(co_await queue.try_pop_front(), std::nullopt);
  }());
}

TEST_F(CATEGORY, pop_front_waits_for_data) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex_queue<int> queue;

    auto result = tmc::spawn(
                    [](tmc::mutex_queue<int>& Queue) -> tmc::task<int> {
                      co_return co_await Queue.pop_front();
                    }(queue)
    )
                    .fork();

    co_await tmc::yield();
    co_await queue.push_back(42);

    EXPECT_EQ(co_await std::move(result), 42);
    EXPECT_EQ(co_await queue.try_pop_front(), std::nullopt);
  }());
}

TEST_F(CATEGORY, pop_back_waits_for_data) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::mutex_queue<int> queue;

    auto result = tmc::spawn(
                    [](tmc::mutex_queue<int>& Queue) -> tmc::task<int> {
                      co_return co_await Queue.pop_back();
                    }(queue)
    )
                    .fork();

    co_await tmc::yield();
    co_await queue.push_front(42);

    EXPECT_EQ(co_await std::move(result), 42);
    EXPECT_EQ(co_await queue.try_pop_back(), std::nullopt);
  }());
}

#undef CATEGORY

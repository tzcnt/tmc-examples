#include "test_common.hpp"
#include "tmc/bounded_queue.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <thread>

#define CATEGORY test_bounded_queue

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

template <size_t Pack, size_t Cap = 1>
struct queue_config : tmc::bounded_queue_default_config {
  static inline constexpr size_t Capacity = Cap;
  static inline constexpr size_t PackingLevel = Pack;
};

struct immovable_queue_config : tmc::bounded_queue_default_config {
  static inline constexpr size_t Capacity = 8;
};

struct move_counter {
  int value;
  std::atomic<size_t>* count;

  move_counter(int v, std::atomic<size_t>* c) noexcept : value(v), count(c) {}

  move_counter(move_counter&& Other) noexcept
      : value(Other.value), count(Other.count) {
    Other.count = nullptr;
    if (count != nullptr) {
      ++(*count);
    }
  }

  move_counter& operator=(move_counter&& Other) noexcept {
    value = Other.value;
    count = Other.count;
    Other.count = nullptr;
    if (count != nullptr) {
      ++(*count);
    }
    return *this;
  }

  move_counter() = delete;
  move_counter(const move_counter&) = delete;
  move_counter& operator=(const move_counter&) = delete;
  ~move_counter() = default;
};

struct immovable_destructor_counter {
  size_t value;
  std::atomic<size_t>* count;

  immovable_destructor_counter(size_t v, std::atomic<size_t>* c) noexcept
      : value(v), count(c) {}

  immovable_destructor_counter() = delete;
  immovable_destructor_counter(const immovable_destructor_counter&) = delete;
  immovable_destructor_counter&
  operator=(const immovable_destructor_counter&) = delete;
  immovable_destructor_counter(immovable_destructor_counter&&) = delete;
  immovable_destructor_counter&
  operator=(immovable_destructor_counter&&) = delete;

  ~immovable_destructor_counter() { ++(*count); }
};

template <size_t PackingLevel, typename Executor>
void do_queue_test(Executor& Exec) {
  test_async_main(Exec, []() -> tmc::task<void> {
    static constexpr size_t NITEMS = 1000;

    struct result {
      size_t count;
      size_t sum;
    };

    auto queue = tmc::bounded_queue<size_t, queue_config<PackingLevel>>{};

    auto results = co_await tmc::spawn_tuple(
      [&queue]() -> tmc::task<size_t> {
        for (size_t i = 0; i < NITEMS; ++i) {
          EXPECT_TRUE(co_await queue.push(i));
        }
        co_return NITEMS;
      }(),
      [&queue]() -> tmc::task<result> {
        size_t count = 0;
        size_t sum = 0;
        for (size_t i = 0; i < NITEMS; ++i) {
          auto value = co_await queue.pull();
          EXPECT_TRUE(value.has_value());
          if (!value.has_value()) {
            co_return result{count, sum};
          }
          EXPECT_EQ(value.value(), i);
          sum += value.value();
          ++count;
        }
        co_return result{count, sum};
      }()
    );

    auto& prod = std::get<0>(results);
    auto& cons = std::get<1>(results);

    EXPECT_EQ(prod, NITEMS);
    EXPECT_EQ(cons.count, NITEMS);

    size_t expectedSum = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(cons.sum, expectedSum);
  }());
}

TEST_F(CATEGORY, config_sweep) {
  do_queue_test<0>(ex());
  do_queue_test<1>(ex());
}

TEST_F(CATEGORY, pull_zc_and_start_pull_zc) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto queue = tmc::bounded_queue<move_counter, queue_config<0>>{};
    std::atomic<size_t> moves{0};

    auto started = queue.start_pull_zc();
    EXPECT_FALSE(static_cast<bool>(started));
    EXPECT_FALSE(started.refresh_ready());

    EXPECT_TRUE(co_await queue.push(42, &moves));

    EXPECT_TRUE(started.refresh_ready());
    EXPECT_TRUE(static_cast<bool>(started));

    {
      auto scope = co_await std::move(started).pull_zc();
      EXPECT_TRUE(scope.has_value());
      if (!scope.has_value()) {
        co_return;
      }
      EXPECT_EQ(scope->get().value, 42);
      EXPECT_EQ((*scope)->value, 42);
      EXPECT_EQ(scope.value().operator->()->value, 42);
      EXPECT_EQ(moves.load(), 0u);
    }
  }());
}

TEST_F(CATEGORY, pull_zc_immovable) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto queue = tmc::bounded_queue<
      immovable_destructor_counter, immovable_queue_config>{};
    std::array<std::atomic<size_t>, 8> destroys{};

    for (size_t i = 0; i < destroys.size(); ++i) {
      EXPECT_TRUE(co_await queue.push(i, &destroys[i]));
    }

    for (size_t i = 0; i < destroys.size(); ++i) {
      auto scope = co_await queue.pull_zc();
      EXPECT_TRUE(scope.has_value());
      if (!scope.has_value()) {
        co_return;
      }
      EXPECT_EQ(scope->get().value, i);
    }

    for (auto& destroy : destroys) {
      EXPECT_EQ(destroy.load(), 1u);
    }
  }());
}

TEST_F(CATEGORY, close_rejects_pushes_and_ends_reads) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto queue = tmc::bounded_queue<size_t, queue_config<0>>{};

    EXPECT_TRUE(co_await queue.push(5u));
    queue.close();

    EXPECT_FALSE(co_await queue.push(6u));

    auto first = co_await queue.pull();
    EXPECT_TRUE(first.has_value());
    if (!first.has_value()) {
      co_return;
    }
    EXPECT_EQ(first.value(), 5u);

    auto second = co_await queue.pull();
    EXPECT_FALSE(second.has_value());

    auto started = queue.start_pull_zc();
    EXPECT_TRUE(started.refresh_ready());
    auto scope = co_await std::move(started).pull_zc();
    EXPECT_FALSE(scope.has_value());
  }());
}

TEST_F(CATEGORY, pull_zc_closed) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto queue = tmc::bounded_queue<size_t, queue_config<0, 2>>{};

    EXPECT_TRUE(co_await queue.push(1u));
    EXPECT_TRUE(co_await queue.push(2u));
    queue.close();

    auto first = co_await queue.pull_zc();
    EXPECT_TRUE(first.has_value());
    if (!first.has_value()) {
      co_return;
    }
    EXPECT_EQ(first->get(), 1u);
    first.reset();

    auto second = co_await queue.pull_zc();
    EXPECT_TRUE(second.has_value());
    if (!second.has_value()) {
      co_return;
    }
    EXPECT_EQ(second->get(), 2u);
    second.reset();

    auto third = co_await queue.pull_zc();
    EXPECT_FALSE(third.has_value());
  }());
}

TEST_F(CATEGORY, drain_empty_queue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto queue = tmc::bounded_queue<size_t, queue_config<0>>{};

    co_await queue.drain();
    EXPECT_FALSE(co_await queue.push(1u));

    auto value = co_await queue.pull();
    EXPECT_FALSE(value.has_value());
  }());
}

#undef CATEGORY

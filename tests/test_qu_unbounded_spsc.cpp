#include "test_common.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/qu_unbounded_spsc.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <ranges>

#define CATEGORY test_qu_unbounded_spsc

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static constexpr size_t SPSC_TEST_SENTINEL = static_cast<size_t>(-1);

template <size_t Pack>
struct qu_config : tmc::qu_unbounded_spsc_default_config {
  // Use a small block size to ensure that alloc / reclaim is triggered.
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr size_t PackingLevel = Pack;
  static inline constexpr bool ConsumerCanSuspend = true;
};

// This version has to be default constructible
struct spsc_destructor_counter {
  std::atomic<size_t>* count;
  spsc_destructor_counter() noexcept : count{nullptr} {}
  spsc_destructor_counter(std::atomic<size_t>* C) noexcept : count{C} {}
  spsc_destructor_counter(spsc_destructor_counter const& Other) = delete;
  spsc_destructor_counter&
  operator=(spsc_destructor_counter const& Other) = delete;

  spsc_destructor_counter(spsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
  }
  spsc_destructor_counter& operator=(spsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
    return *this;
  }

  ~spsc_destructor_counter() {
    if (count != nullptr) {
      ++(*count);
    }
  }
};

// multiple tests in one to leverage the configuration options in one place
template <size_t PackingLevel, typename Executor>
void do_chan_test(Executor& Exec) {
  test_async_main(Exec, []() -> tmc::task<void> {
    {
      // general test - single push
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto chan = tmc::qu_unbounded_spsc<size_t, qu_config<PackingLevel>>{};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; ++i) {
            Chan.post(i);
          }
          Chan.post(SPSC_TEST_SENTINEL);
          co_return i;
        }(chan),
        [](auto& Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          while (true) {
            auto v = co_await Chan.pull();
            if (*v == SPSC_TEST_SENTINEL) {
              co_return result{count, sum};
            }
            ++count;
            sum += *v;
          }
        }(chan)
      );
      auto& prod = std::get<0>(results);
      auto& cons = std::get<1>(results);
      EXPECT_EQ(NITEMS, prod);
      EXPECT_EQ(NITEMS, cons.count);
      size_t expectedSum = 0;
      for (size_t i = 0; i < NITEMS; ++i) {
        expectedSum += i;
      }
      EXPECT_EQ(expectedSum, cons.sum);
    }
    {
      // general test - post_bulk
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto chan = tmc::qu_unbounded_spsc<size_t, qu_config<PackingLevel>>{};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; i += (NITEMS / 10)) {
            size_t j = i + (NITEMS / 10);
            if (j > NITEMS) {
              j = NITEMS;
            }
            Chan.post_bulk(std::ranges::views::iota(i).begin(), j - i);
          }
          co_return i;
        }(chan),
        [](auto& Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          for (size_t i = 0; i < NITEMS; ++i) {
            auto v = Chan.try_pull();
            while (!v) {
              TMC_CPU_PAUSE();
              v = Chan.try_pull();
            }
            ++count;
            sum += *v;
          }
          co_return result{count, sum};
        }(chan)
      );
      auto& prod = std::get<0>(results);
      auto& cons = std::get<1>(results);
      EXPECT_EQ(NITEMS, prod);
      EXPECT_EQ(NITEMS, cons.count);
      size_t expectedSum = 0;
      for (size_t i = 0; i < NITEMS; ++i) {
        expectedSum += i;
      }
      EXPECT_EQ(expectedSum, cons.sum);
    }
    {
      // destroy chan with data remaining inside
      std::atomic<size_t> count;
      {
        auto chan = tmc::qu_unbounded_spsc<
          spsc_destructor_counter, qu_config<PackingLevel>>{};
        for (size_t i = 0; i < 12; ++i) {
          chan.post(spsc_destructor_counter{&count});
        }

        for (size_t i = 0; i < 7; ++i) {
          auto v = chan.try_pull();
          EXPECT_TRUE(static_cast<bool>(v));
        }

        EXPECT_EQ(count.load(), 7);
      }
      // Now chan goes out of scope; remaining data's destructors are called
      EXPECT_EQ(count.load(), 12);
    }
  }());
}

TEST_F(CATEGORY, config_sweep) {
  do_chan_test<0>(ex());
  do_chan_test<1>(ex());
}

// Test post_bulk of 0 items
TEST_F(CATEGORY, post_bulk_none) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_unbounded_spsc<size_t, qu_config<0>>{};
    size_t i = 0;
    for (; i < 4; ++i) {
      chan.post_bulk(&i, 0);
      chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      chan.post(i);
    }
    for (; i < 8; ++i) {
      chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      chan.post_bulk(std::ranges::views::iota(i).begin(), 1);
    }
    size_t count = 0;
    size_t sum = 0;
    for (size_t j = 0; j < i; ++j) {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(28, sum);
    EXPECT_EQ(8, count);

    auto v = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

struct chan_config_no_suspend : tmc::qu_unbounded_spsc_default_config {
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr bool ConsumerCanSuspend = false;
};

// Verify that try_pull works when ConsumerCanSuspend = false (pull() is
// disabled in this configuration).
TEST_F(CATEGORY, try_pull_no_suspend) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_unbounded_spsc<size_t, chan_config_no_suspend>{};

    // Empty queue: try_pull yields an empty scope.
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    // Post enough items to cross several blocks.
    static constexpr size_t NITEMS = 10;
    for (size_t i = 0; i < NITEMS; ++i) {
      chan.post(i);
    }

    size_t sum = 0;
    size_t count = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(NITEMS, count);
    size_t expectedSum = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, sum);

    // Queue is drained: try_pull again yields an empty scope.
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }
    co_return;
  }());
}

// A type with no default, copy, or move constructor. Can only be created
// in-place via post()'s emplace forwarding.
struct non_movable {
  int value;

  non_movable(int X, int Y) noexcept : value{X + Y} {}

  non_movable() = delete;
  non_movable(non_movable const&) = delete;
  non_movable(non_movable&&) = delete;
  non_movable& operator=(non_movable const&) = delete;
  non_movable& operator=(non_movable&&) = delete;
};

TEST_F(CATEGORY, non_movable_type) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_unbounded_spsc<non_movable, qu_config<0>>{};

    // try_pull on empty queue
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    // Emplace-construct values directly in the queue storage.
    chan.post(1, 2);
    chan.post(3, 4);
    chan.post(5, 6);

    // First value via try_pull
    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(3, v->value);
    }

    // Second value via co_await pull()
    {
      auto v = co_await chan.pull();
      EXPECT_EQ(7, v->value);
    }

    // Third value via try_pull
    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(11, v->value);
    }

    // Queue is drained
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    co_return;
  }());
}

#undef CATEGORY

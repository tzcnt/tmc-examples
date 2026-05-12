#include "test_common.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/detail/qu_mpsc.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <ranges>

#define CATEGORY test_qu_mpsc

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static constexpr size_t MPSC_TEST_SENTINEL = static_cast<size_t>(-1);

template <size_t Pack> struct chan_config : tmc::detail::qu_mpsc_default_config {
  // Use a small block size to ensure that alloc / reclaim is triggered.
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr size_t PackingLevel = Pack;
  static inline constexpr bool ConsumerCanSuspend = true;
};

// This version has to be default constructible
struct mpsc_destructor_counter {
  std::atomic<size_t>* count;
  mpsc_destructor_counter() noexcept : count{nullptr} {}
  mpsc_destructor_counter(std::atomic<size_t>* C) noexcept : count{C} {}
  mpsc_destructor_counter(mpsc_destructor_counter const& Other) = delete;
  mpsc_destructor_counter&
  operator=(mpsc_destructor_counter const& Other) = delete;

  mpsc_destructor_counter(mpsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
  }
  mpsc_destructor_counter& operator=(mpsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
    return *this;
  }

  ~mpsc_destructor_counter() {
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

      auto chan = tmc::detail::qu_mpsc<size_t, chan_config<PackingLevel>>{};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; ++i) {
            Chan.post(i);
          }
          Chan.post(MPSC_TEST_SENTINEL);
          co_return i;
        }(chan),
        [](auto& Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          while (true) {
            size_t v = co_await Chan.pull();
            if (v == MPSC_TEST_SENTINEL) {
              co_return result{count, sum};
            }
            ++count;
            sum += v;
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

      auto chan = tmc::detail::qu_mpsc<size_t, chan_config<PackingLevel>>{};

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
            size_t v;
            while (!Chan.try_pull(v)) {
              TMC_CPU_PAUSE();
            }
            ++count;
            sum += v;
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
        auto chan = tmc::detail::qu_mpsc<
          mpsc_destructor_counter, chan_config<PackingLevel>>{};
        for (size_t i = 0; i < 12; ++i) {
          chan.post(mpsc_destructor_counter{&count});
        }

        for (size_t i = 0; i < 7; ++i) {
          mpsc_destructor_counter v;
          auto ok = chan.try_pull(v);
          EXPECT_TRUE(ok);
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
    auto chan = tmc::detail::qu_mpsc<size_t, chan_config<0>>{};
    size_t i = 0;
    bool ok;
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
      size_t v;
      EXPECT_TRUE(chan.try_pull(v));
      sum += v;
      ++count;
    }
    EXPECT_EQ(28, sum);
    EXPECT_EQ(8, count);

    size_t v;
    ok = chan.try_pull(v);
    EXPECT_FALSE(ok);
    co_return;
  }());
}

#undef CATEGORY

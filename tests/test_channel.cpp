#include "test_common.hpp"
#include "tmc/channel.hpp"
#include "tmc/detail/compat.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <gtest/gtest.h>
#include <optional>
#include <ranges>
#include <thread>

#define CATEGORY test_channel

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

template <size_t Pack> struct chan_config : tmc::chan_default_config {
  // Use a small block size to ensure that alloc / reclaim is triggered.
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr size_t PackingLevel = Pack;
};

// multiple tests in one to leverage the configuration options in one place
template <size_t PackingLevel, typename Executor>
void do_chan_test(Executor& Exec, size_t HeavyLoadThreshold, bool ReuseBlocks) {
  test_async_main(Exec, [](size_t Threshold, bool Reuse) -> tmc::task<void> {
    {
      // general test - single push
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto chan = tmc::make_channel<size_t, chan_config<PackingLevel>>()
                    .set_reuse_blocks(Reuse)
                    .set_heavy_load_threshold(Threshold);

      auto results = co_await tmc::spawn_tuple(
        [](auto Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; ++i) {
            bool ok = co_await Chan.push(i);
            EXPECT_EQ(true, ok);
          }
          co_await Chan.drain();
          co_return i;
        }(chan),
        [](auto Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          std::optional<size_t> v = co_await Chan.pull();
          while (v.has_value()) {
            sum += v.value();
            ++count;
            v = co_await Chan.pull();
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
      // general test - post_bulk
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto chan = tmc::make_channel<size_t, chan_config<PackingLevel>>()
                    .set_reuse_blocks(Reuse)
                    .set_heavy_load_threshold(Threshold);

      auto results = co_await tmc::spawn_tuple(
        [](auto Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; i += (NITEMS / 10)) {
            size_t j = i + (NITEMS / 10);
            if (j > NITEMS) {
              j = NITEMS;
            }
            bool ok = Chan.post_bulk(std::ranges::views::iota(i, j));
            EXPECT_EQ(true, ok);
          }
          co_await Chan.drain();
          co_return i;
        }(chan),
        [](auto Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          std::optional<size_t> v = co_await Chan.pull();
          while (v.has_value()) {
            sum += v.value();
            ++count;
            v = co_await Chan.pull();
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
      // make consumer spin wait
      auto chan = tmc::make_channel<size_t, chan_config<PackingLevel>>()
                    .set_reuse_blocks(Reuse)
                    .set_heavy_load_threshold(Threshold)
                    .set_consumer_spins(TMC_ALL_ONES);
      auto cons = tmc::spawn([](auto Chan) -> tmc::task<void> {
                    auto v = co_await Chan.pull();
                    EXPECT_TRUE(v.has_value());
                    EXPECT_EQ(v.value(), 5);
                  }(chan))
                    .fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      chan.post(5);
      co_await std::move(cons);
    }
    {
      // destroy chan with data remaining inside
      std::atomic<size_t> count;
      {
        auto chan =
          tmc::make_channel<destructor_counter, chan_config<PackingLevel>>()
            .set_reuse_blocks(Reuse)
            .set_heavy_load_threshold(Threshold);
        for (size_t i = 0; i < 12; ++i) {
          chan.post(destructor_counter{&count});
        }

        for (size_t i = 0; i < 7; ++i) {
          co_await chan.pull();
        }

        EXPECT_EQ(count.load(), 7);
      }
      // Now chan goes out of scope; remaining data's destructors are called
      EXPECT_EQ(count.load(), 12);
    }
    {
      // producer post / post_bulk after chan closed
      auto chan = tmc::make_channel<size_t, chan_config<PackingLevel>>()
                    .set_reuse_blocks(Reuse)
                    .set_heavy_load_threshold(Threshold);
      chan.close();
      auto p = chan.post(5);
      EXPECT_FALSE(p);
      std::vector<size_t> vs{0, 1, 2, 3, 4};
      auto p1 = chan.post_bulk(vs.begin(), 5);
      EXPECT_FALSE(p1);
      auto p2 = chan.post_bulk(vs.begin(), vs.end());
      EXPECT_FALSE(p2);
      auto p3 = chan.post_bulk(std::ranges::views::iota(0, 5));
      EXPECT_FALSE(p3);
    }
    {
      // drain while there is a waiting consumer
      auto chan = tmc::make_channel<size_t, chan_config<PackingLevel>>()
                    .set_reuse_blocks(Reuse)
                    .set_heavy_load_threshold(Threshold);
      std::array<tmc::task<void>, 5> cons;
      for (size_t i = 0; i < 5; ++i) {
        cons[i] = [](auto Chan) -> tmc::task<void> {
          auto v = co_await Chan.pull();
          EXPECT_FALSE(v.has_value());
        }(chan);
      }
      auto t = tmc::spawn_many<5>(cons.data()).fork();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      co_await chan.drain();
      co_await std::move(t);
    }
  }(HeavyLoadThreshold, ReuseBlocks));
}

TEST_F(CATEGORY, misc) {
  do_chan_test<0>(ex(), 0, false);
  do_chan_test<0>(ex(), 0, true);
  do_chan_test<0>(ex(), 1, false);
  do_chan_test<0>(ex(), 1, true);
  do_chan_test<1>(ex(), 0, false);
  do_chan_test<1>(ex(), 0, true);
  do_chan_test<1>(ex(), 1, false);
  do_chan_test<1>(ex(), 1, true);
  if constexpr (TMC_PLATFORM_BITS == 64) {
    do_chan_test<2>(ex(), 0, false);
    do_chan_test<2>(ex(), 0, true);
    do_chan_test<2>(ex(), 1, false);
    do_chan_test<2>(ex(), 1, true);
  }
}

// Running 1 consumer and 1 producer at the same time on a single thread
// To ensure there are no deadlocks
TEST_F(CATEGORY, push_single_threaded) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    static constexpr size_t NITEMS = 100000;
    auto chan = tmc::make_channel<size_t>();
    struct result {
      size_t count;
      size_t sum;
    };

    auto results = co_await tmc::spawn_tuple(
      [](auto Chan) -> tmc::task<size_t> {
        size_t i = 0;
        for (; i < NITEMS; ++i) {
          bool ok;
          if ((i & 0x2) == 0) {
            // 2 pushes
            ok = co_await Chan.push(i);
          } else {
            // then 2 post_bulks
            ok = Chan.post_bulk(std::ranges::views::iota(i, i + 1));
          }
          EXPECT_EQ(true, ok);
        }
        co_await Chan.drain();
        co_return i;
      }(chan),
      [](auto Chan) -> tmc::task<result> {
        size_t count = 0;
        size_t sum = 0;
        std::optional<size_t> v = co_await Chan.pull();
        while (v.has_value()) {
          sum += v.value();
          ++count;
          v = co_await Chan.pull();
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
  }());
}

// Test post_bulk of 0 items
TEST_F(CATEGORY, post_bulk_none) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::make_channel<size_t, chan_config<0>>();
    size_t i = 0;
    bool ok;
    for (; i < 4; ++i) {
      ok = chan.post_bulk(std::ranges::views::iota(i, i));
      EXPECT_EQ(true, ok);
      ok = chan.post_bulk(std::ranges::views::iota(i, i));
      EXPECT_EQ(true, ok);
      ok = chan.post(i);
      EXPECT_EQ(true, ok);
    }
    for (; i < 8; ++i) {
      ok = chan.post_bulk(std::ranges::views::iota(i, i));
      EXPECT_EQ(true, ok);
      ok = chan.post_bulk(std::ranges::views::iota(i, i));
      EXPECT_EQ(true, ok);
      ok = chan.post_bulk(std::ranges::views::iota(i, i + 1));
      EXPECT_EQ(true, ok);
    }
    size_t count = 0;
    size_t sum = 0;
    for (size_t j = 0; j < i; ++j) {
      std::optional<size_t> v = co_await chan.pull();
      sum += v.value();
      ++count;
    }
    chan.close();
    EXPECT_EQ(28, sum);
    EXPECT_EQ(8, count);

    std::optional<size_t> v = co_await chan.pull();
    EXPECT_FALSE(v.has_value());

    ok = chan.post_bulk(std::ranges::views::iota(1, 1));
    EXPECT_EQ(false, ok);
    ok = chan.post_bulk(std::ranges::views::iota(1, 2));
    EXPECT_EQ(false, ok);
    ok = chan.post(5);
    EXPECT_EQ(false, ok);
  }());
}

#undef CATEGORY

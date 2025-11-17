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
void do_chan_test(
  Executor& Exec, size_t HeavyLoadThreshold, bool ReuseBlocks, bool TryPull
) {
  test_async_main(
    Exec, [](size_t Threshold, bool Reuse, bool Try) -> tmc::task<void> {
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
          [](auto Chan, bool DoTry) -> tmc::task<result> {
            size_t count = 0;
            size_t sum = 0;
            while (true) {
              if (!DoTry) {
                auto data = co_await Chan.pull();
                if (data.has_value()) {
                  ++count;
                  sum += data.value();
                } else {
                  co_return result{count, sum};
                }
              } else {
                auto data = Chan.try_pull();
                switch (data.index()) {
                case tmc::chan_err::OK:
                  ++count;
                  sum += std::get<0>(data);
                  break;
                case tmc::chan_err::EMPTY:
                  co_await tmc::reschedule();
                  break;
                case tmc::chan_err::CLOSED:
                  co_return result{count, sum};
                default:
                  break;
                }
              }
            }
          }(chan, Try)
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
          [](auto Chan, bool DoTry) -> tmc::task<result> {
            size_t count = 0;
            size_t sum = 0;
            while (true) {
              if (!DoTry) {
                auto data = co_await Chan.pull();
                if (data.has_value()) {
                  ++count;
                  sum += data.value();
                } else {
                  co_return result{count, sum};
                }
              } else {
                auto data = Chan.try_pull();
                switch (data.index()) {
                case tmc::chan_err::OK:
                  ++count;
                  sum += std::get<0>(data);
                  break;
                case tmc::chan_err::EMPTY:
                  co_await tmc::reschedule();
                  break;
                case tmc::chan_err::CLOSED:
                  co_return result{count, sum};
                default:
                  break;
                }
              }
            }
          }(chan, Try)
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
        chan.post(5u);
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
        auto p = chan.post(5u);
        EXPECT_FALSE(p);
        std::vector<size_t> vs{0, 1, 2, 3, 4};
        auto p1 = chan.post_bulk(vs.begin(), 5);
        EXPECT_FALSE(p1);
        auto p2 = chan.post_bulk(vs.begin(), vs.end());
        EXPECT_FALSE(p2);
        auto p3 = chan.post_bulk(std::ranges::views::iota(0u, 5u));
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
    }(HeavyLoadThreshold, ReuseBlocks, TryPull)
  );
}

TEST_F(CATEGORY, config_sweep) {
  do_chan_test<0>(ex(), 0, false, false);
  do_chan_test<0>(ex(), 0, true, false);
  do_chan_test<0>(ex(), 1, false, false);
  do_chan_test<0>(ex(), 1, true, false);
  do_chan_test<1>(ex(), 0, false, false);
  do_chan_test<1>(ex(), 0, true, false);
  do_chan_test<1>(ex(), 1, false, false);
  do_chan_test<1>(ex(), 1, true, false);
  if constexpr (TMC_PLATFORM_BITS == 64) {
    do_chan_test<2>(ex(), 0, false, false);
    do_chan_test<2>(ex(), 0, true, false);
    do_chan_test<2>(ex(), 1, false, false);
    do_chan_test<2>(ex(), 1, true, false);
  }
}

TEST_F(CATEGORY, config_sweep_try_pull) {
  do_chan_test<0>(ex(), 0, false, true);
  do_chan_test<0>(ex(), 0, true, true);
  do_chan_test<0>(ex(), 1, false, true);
  do_chan_test<0>(ex(), 1, true, true);
  do_chan_test<1>(ex(), 0, false, true);
  do_chan_test<1>(ex(), 0, true, true);
  do_chan_test<1>(ex(), 1, false, true);
  do_chan_test<1>(ex(), 1, true, true);
  if constexpr (TMC_PLATFORM_BITS == 64) {
    do_chan_test<2>(ex(), 0, false, true);
    do_chan_test<2>(ex(), 0, true, true);
    do_chan_test<2>(ex(), 1, false, true);
    do_chan_test<2>(ex(), 1, true, true);
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

    ok = chan.post_bulk(std::ranges::views::iota(1u, 1u));
    EXPECT_EQ(false, ok);
    ok = chan.post_bulk(std::ranges::views::iota(1u, 2u));
    EXPECT_EQ(false, ok);
    ok = chan.post(5u);
    EXPECT_EQ(false, ok);
  }());
}

// Demonstrate passing a move-only type through the channel.
struct move_only_type {
  int value;

  move_only_type(int input) : value(input) {}
  move_only_type& operator=(move_only_type&&) = default;
  move_only_type(move_only_type&&) = default;
  ~move_only_type() = default;

  // No default or copy constructor
  move_only_type() = delete;
  move_only_type(const move_only_type&) = delete;
  move_only_type& operator=(const move_only_type&) = delete;
};

TEST_F(CATEGORY, move_only_type) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto chan = tmc::make_channel<move_only_type, chan_config<0>>();

    chan.post(move_only_type(0));

    auto mt = move_only_type(1);
    chan.post(std::move(mt));

    co_await chan.push(move_only_type(2));

    auto mt1 = move_only_type(3);
    co_await chan.push(std::move(mt1));

    auto v0 = co_await chan.pull();
    EXPECT_TRUE(v0.has_value());
    EXPECT_EQ(v0.value().value, 0);

    auto v1 = chan.try_pull();
    EXPECT_EQ(v1.index(), tmc::chan_err::OK);
    EXPECT_EQ(std::get<0>(v1).value, 1);

    auto v2 = co_await chan.pull();
    EXPECT_TRUE(v2.has_value());
    EXPECT_EQ(v2.value().value, 2);

    auto v3 = chan.try_pull();
    EXPECT_EQ(v3.index(), tmc::chan_err::OK);
    EXPECT_EQ(std::get<0>(v3).value, 3);
  }());
}

#undef CATEGORY

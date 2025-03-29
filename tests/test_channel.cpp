#include "test_common.hpp"
#include "tmc/channel.hpp"

#include <gtest/gtest.h>

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

template <size_t PackingLevel, typename Executor>
void do_chan_test(Executor& Exec, size_t Threshold, bool ReuseBlocks) {
  test_async_main(
    Exec,
    [](size_t Threshold, bool ReuseBlocks) -> tmc::task<void> {
      static constexpr int NITEMS = 1000;
      struct result {
        int count;
        int sum;
      };

      auto chan = tmc::make_channel<int, chan_config<PackingLevel>>()
                    .set_reuse_blocks(ReuseBlocks)
                    .set_heavy_load_threshold(Threshold);

      auto results = co_await tmc::spawn_tuple(
        [](auto Chan) -> tmc::task<int> {
          int i = 0;
          for (; i < NITEMS; ++i) {
            bool ok = co_await Chan.push(i);
            EXPECT_EQ(true, ok);
          }
          co_await Chan.drain();
          co_return i;
        }(chan),
        [](auto Chan) -> tmc::task<result> {
          int count = 0;
          int sum = 0;
          std::optional<int> v = co_await Chan.pull();
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
      int expectedSum = 0;
      for (int i = 0; i < NITEMS; ++i) {
        expectedSum += i;
      }
      EXPECT_EQ(expectedSum, cons.sum);
    }(Threshold, ReuseBlocks)
  );
}

TEST_F(CATEGORY, push) {
  do_chan_test<0>(ex(), 0, false);
  do_chan_test<0>(ex(), 0, true);
  do_chan_test<0>(ex(), 1, false);
  do_chan_test<0>(ex(), 1, true);
  do_chan_test<1>(ex(), 0, false);
  do_chan_test<1>(ex(), 0, true);
  do_chan_test<1>(ex(), 1, false);
  do_chan_test<1>(ex(), 1, true);
  do_chan_test<2>(ex(), 0, false);
  do_chan_test<2>(ex(), 0, true);
  do_chan_test<2>(ex(), 1, false);
  do_chan_test<2>(ex(), 1, true);
}

TEST_F(CATEGORY, push_single_threaded) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    static constexpr int NITEMS = 100000;
    auto chan = tmc::make_channel<int>();
    struct result {
      int count;
      int sum;
    };

    auto results = co_await tmc::spawn_tuple(
      [](auto Chan) -> tmc::task<int> {
        int i = 0;
        for (; i < NITEMS; ++i) {
          bool ok = co_await Chan.push(i);
          EXPECT_EQ(true, ok);
        }
        co_await Chan.drain();
        co_return i;
      }(chan),
      [](auto Chan) -> tmc::task<result> {
        int count = 0;
        int sum = 0;
        std::optional<int> v = co_await Chan.pull();
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
    int expectedSum = 0;
    for (int i = 0; i < NITEMS; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, cons.sum);
  }());
}

#undef CATEGORY
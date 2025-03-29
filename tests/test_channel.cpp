#include "test_common.hpp"
#include "tmc/channel.hpp"

#include <numeric>

#include <gtest/gtest.h>

#define CATEGORY test_channel

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

struct chan_config : tmc::chan_default_config {
  // Use a small block size to ensure that alloc / reclaim is triggered.
  static inline constexpr size_t BlockSize = 2;
};

TEST_F(CATEGORY, post) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr int NITEMS = 1000;
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
        Chan.drain_wait();
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
#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"

#include <gtest/gtest.h>
#include <ranges>

#define CATEGORY test_ex_asio

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::asio_executor().init(); }

  static void TearDownTestSuite() { tmc::asio_executor().teardown(); }
};

TEST_F(CATEGORY, post_waitable_coro) {
  tmc::post_waitable(
    tmc::asio_executor(), []() -> tmc::task<void> { co_return; }(), 0
  )
    .get();
  tmc::post_waitable(tmc::asio_executor(), empty_task(), 0).get();

  auto x = tmc::post_waitable(
             tmc::asio_executor(), []() -> tmc::task<int> { co_return 1; }(), 0
  )
             .get();
  EXPECT_EQ(x, 1);
  auto y = tmc::post_waitable(tmc::asio_executor(), int_task(), 0).get();
  EXPECT_EQ(y, 1);
}

TEST_F(CATEGORY, post_waitable_func) {
  tmc::post_waitable(tmc::asio_executor(), []() -> void {}, 0).get();
  tmc::post_waitable(tmc::asio_executor(), empty_func, 0).get();

  auto x = tmc::post_waitable(
             tmc::asio_executor(), []() -> int { return 1; }, 0
  )
             .get();
  EXPECT_EQ(x, 1);
  auto y = tmc::post_waitable(tmc::asio_executor(), int_func, 0).get();
  EXPECT_EQ(y, 1);
}

TEST_F(CATEGORY, post_bulk_waitable_coro) {
  tmc::post_bulk_waitable(
    tmc::asio_executor(),
    tmc::iter_adapter(0, [](int i) -> tmc::task<void> { co_return; }), 10, 0
  )
    .get();

  {
    std::array<int, 2> results = {5, 5};
    tmc::post_bulk_waitable(
      tmc::asio_executor(),
      (std::ranges::views::iota(0) |
       std::ranges::views::transform([&results](int i) -> tmc::task<void> {
         return [](int* out, int val) -> tmc::task<void> {
           *out = val;
           co_return;
         }(&results[i], i);
       })
      ).begin(),
      2, 0
    )
      .wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_waitable_func) {
  {
    std::array<int, 2> results = {5, 5};
    auto range = (std::ranges::views::iota(0UL) |
                  std::ranges::views::transform([&](int i) {
                    return [&results, i = i]() { results[i] = i; };
                  })
    ).begin();
    tmc::post_bulk_waitable(tmc::asio_executor(), range, 2, 0).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

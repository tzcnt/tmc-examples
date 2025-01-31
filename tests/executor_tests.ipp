#include "test_common.hpp"

#include <gtest/gtest.h>
#include <ranges>

#define CAT_TEST(CATEGORY, NAME) TEST_F(CATEGORY, NAME)

TEST_F(CATEGORY, init) { EXPECT_EQ(ex().thread_count(), 64); }

TEST_F(CATEGORY, post_waitable_coro) {
  tmc::post_waitable(ex(), []() -> tmc::task<void> { co_return; }(), 0).get();
  tmc::post_waitable(ex(), empty_task(), 0).get();

  auto x = tmc::post_waitable(
             ex(), []() -> tmc::task<int> { co_return 1; }(), 0
  )
             .get();
  EXPECT_EQ(x, 1);
  auto y = tmc::post_waitable(ex(), int_task(), 0).get();
  EXPECT_EQ(y, 1);
}

TEST_F(CATEGORY, post_waitable_func) {
  tmc::post_waitable(ex(), []() -> void {}, 0).get();
  tmc::post_waitable(ex(), empty_func, 0).get();

  auto x = tmc::post_waitable(ex(), []() -> int { return 1; }, 0).get();
  EXPECT_EQ(x, 1);
  auto y = tmc::post_waitable(ex(), int_func, 0).get();
  EXPECT_EQ(y, 1);
}

TEST_F(CATEGORY, post_bulk_waitable_coro) {
  tmc::post_bulk_waitable(
    ex(), tmc::iter_adapter(0, [](int i) -> tmc::task<void> { co_return; }), 10,
    0
  )
    .get();

  {
    std::array<int, 2> results = {5, 5};
    tmc::post_bulk_waitable(
      ex(),
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
    tmc::post_bulk_waitable(ex(), range, 2, 0).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

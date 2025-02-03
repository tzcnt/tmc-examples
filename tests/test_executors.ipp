#include "test_common.hpp"

#include <gtest/gtest.h>
#include <ranges>

#define CAT_TEST(CATEGORY, NAME) TEST_F(CATEGORY, NAME)

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

TEST_F(CATEGORY, async_main) {
  test_async_main(ex(), []() -> tmc::task<void> { co_await empty_task(); }());
  int x = test_async_main_int(ex(), []() -> tmc::task<int> {
    int x = co_await int_task();
    co_return x;
  }());
  EXPECT_EQ(x, 1);
}

TEST_F(CATEGORY, spawn_func) {
  const size_t NTASKS = 10;
  const size_t NCHECKS = 8;
  std::array<size_t, NTASKS * NCHECKS> results;
  auto tasks =
    std::ranges::views::iota(
      static_cast<size_t>(0), static_cast<size_t>(NTASKS)
    ) |
    std::ranges::views::transform([&results](size_t slot) -> tmc::task<void> {
      return [](
               std::array<size_t, NTASKS * NCHECKS>& results, size_t base
             ) -> tmc::task<void> {
        // These inc() calls are not thread-safe but are synchronized because we
        // co_await throughout
        size_t idx = base;
        inc(results, idx);
        co_await tmc::spawn_func([&results, &idx]() { inc(results, idx); });
        EXPECT_EQ(idx, base + 2);

        idx = co_await tmc::spawn_func([&results, idx]() mutable {
          inc(results, idx);
          return idx;
        });
        EXPECT_EQ(idx, base + 3);
        co_await tmc::spawn(
          [](
            std::array<size_t, NTASKS * NCHECKS>& results, size_t& idx
          ) -> tmc::task<void> {
            inc(results, idx);
            co_await tmc::yield();
            inc(results, idx);
          }(results, idx)
        );
        EXPECT_EQ(idx, base + 5);
        // in this case, the spawned function returns a task,
        // and a 2nd co_await is required
        co_await co_await tmc::spawn_func(
          [&idx, &results]() -> tmc::task<void> {
            return [](
                     std::array<size_t, NTASKS * NCHECKS>& results, size_t& idx
                   ) -> tmc::task<void> {
              inc(results, idx);
              co_await tmc::yield();
              inc(results, idx);
              co_return;
            }(results, idx);
          }
        );
        EXPECT_EQ(idx, base + 7);

        auto t = tmc::spawn_func([&results, &idx]() { inc(results, idx); });
        co_await std::move(t).run_on(ex()).resume_on(ex()).with_priority(0);
        co_return;
      }(results, slot * NCHECKS);
    });
  auto future = post_bulk_waitable(ex(), tasks.begin(), NTASKS, 0);
  future.get();
  for (size_t i = 0; i < results.size(); ++i) {
    EXPECT_EQ(results[i], i);
  }
}

TEST_F(CATEGORY, spawn_coro) {
  const size_t NTASKS = 10;
  const size_t NCHECKS = 5;
  std::array<size_t, NTASKS * NCHECKS> results;
  auto tasks =
    std::ranges::views::iota(
      static_cast<size_t>(0), static_cast<size_t>(NTASKS)
    ) |
    std::ranges::views::transform([&results](size_t slot) -> tmc::task<void> {
      return [](
               std::array<size_t, NTASKS * NCHECKS>& results, size_t base
             ) -> tmc::task<void> {
        size_t idx = base;
        inc(results, idx);
        co_await tmc::spawn(inc_task(results, idx));
        EXPECT_EQ(idx, base + 2);

        auto early = tmc::spawn(inc_task_int(results, idx)).run_early();
        idx = co_await tmc::spawn(inc_task_int(results, idx + 1));
        auto r = co_await std::move(early);
        EXPECT_EQ(r, base + 3);
        EXPECT_EQ(idx, base + 4);

        auto t = tmc::spawn(inc_task(results, idx));
        co_await std::move(t).run_on(ex()).resume_on(ex()).with_priority(0);
        co_return;
      }(results, slot * NCHECKS);
    });
  auto future = post_bulk_waitable(ex(), tasks.begin(), NTASKS, 0);
  future.get();
  for (size_t i = 0; i < results.size(); ++i) {
    EXPECT_EQ(results[i], i);
  }
}
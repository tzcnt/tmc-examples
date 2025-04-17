#include "test_common.hpp"

#include <gtest/gtest.h>
#include <ranges>

TEST_F(CATEGORY, post_coro) {
  std::atomic<int> x = 0;
  tmc::post(
    ex(),
    [](std::atomic<int>& i) -> tmc::task<void> {
      ++i;
      i.notify_all();
      co_return;
    }(x),
    0
  );
  x.wait(0);
  EXPECT_EQ(x, 1);
  tmc::post(ex(), capturing_task(x), 0);
  x.wait(1);
  EXPECT_EQ(x, 2);
}

TEST_F(CATEGORY, post_func) {
  std::atomic<int> x = 0;
  tmc::post(
    ex(),
    [&x]() {
      ++x;
      x.notify_all();
    },
    0
  );
  x.wait(0);
  EXPECT_EQ(x, 1);
}

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

TEST_F(CATEGORY, post_bulk_coro_begin_count) {
  {
    std::atomic<int> flag = 0;
    std::array<int, 2> results = {5, 5};
    tmc::post_bulk(
      ex(),
      tmc::iter_adapter(
        0,
        [&results, &flag](int i) -> tmc::task<void> {
          return [](int* out, int val, std::atomic<int>& x) -> tmc::task<void> {
            *out = val;
            ++x;
            x.notify_all();
            co_return;
          }(&results[i], i, flag);
        }
      ),
      2, 0
    );
    flag.wait(0);
    if (flag != 2) {
      flag.wait(1);
    }
    EXPECT_EQ(flag, 2);

    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }

  {
    std::atomic<int> flag = 0;
    std::array<int, 2> results = {5, 5};
    tmc::post_bulk(
      ex(),
      (std::ranges::views::iota(0) |
       std::ranges::views::transform(
         [&results, &flag](int i) -> tmc::task<void> {
           return
             [](int* out, int val, std::atomic<int>& x) -> tmc::task<void> {
               *out = val;
               ++x;
               x.notify_all();
               co_return;
             }(&results[i], i, flag);
         }
       )
      ).begin(),
      2, 0
    );
    flag.wait(0);
    if (flag != 2) {
      flag.wait(1);
    }
    EXPECT_EQ(flag, 2);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_coro_begin_end) {
  {
    std::atomic<int> flag = 0;
    std::array<int, 2> results = {5, 5};
    auto range =
      std::ranges::views::iota(0, 2) |
      std::ranges::views::transform(
        [&results, &flag](int i) -> tmc::task<void> {
          return [](int* out, int val, std::atomic<int>& x) -> tmc::task<void> {
            *out = val;
            ++x;
            x.notify_all();
            co_return;
          }(&results[i], i, flag);
        }
      );
    tmc::post_bulk(ex(), range.begin(), range.end(), 0);
    flag.wait(0);
    if (flag != 2) {
      flag.wait(1);
    }
    EXPECT_EQ(flag, 2);

    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_coro_range) {
  {
    std::atomic<int> flag = 0;
    std::array<int, 2> results = {5, 5};
    auto range =
      std::ranges::views::iota(0, 2) |
      std::ranges::views::transform(
        [&results, &flag](int i) -> tmc::task<void> {
          return [](int* out, int val, std::atomic<int>& x) -> tmc::task<void> {
            *out = val;
            ++x;
            x.notify_all();
            co_return;
          }(&results[i], i, flag);
        }
      );
    tmc::post_bulk(ex(), range, 0);
    flag.wait(0);
    if (flag != 2) {
      flag.wait(1);
    }
    EXPECT_EQ(flag, 2);

    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_func_begin_count) {
  {
    std::atomic<int> flag = 0;
    std::array<int, 2> results = {5, 5};
    auto ts =
      std::ranges::views::iota(0) | std::ranges::views::transform([&](int i) {
        return [&results, &flag, i]() {
          results[i] = i;
          ++flag;
          flag.notify_all();
        };
      });
    tmc::post_bulk(ex(), ts.begin(), 2, 0);
    flag.wait(0);
    if (flag != 2) {
      flag.wait(1);
    }
    EXPECT_EQ(flag, 2);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_func_begin_end) {
  {
    std::atomic<int> flag = 0;
    std::array<int, 2> results = {5, 5};
    auto ts = std::ranges::views::iota(0, 2) |
              std::ranges::views::transform([&](int i) {
                return [&results, &flag, i]() {
                  results[i] = i;
                  ++flag;
                  flag.notify_all();
                };
              });
    tmc::post_bulk(ex(), ts.begin(), ts.end(), 0);
    flag.wait(0);
    if (flag != 2) {
      flag.wait(1);
    }
    EXPECT_EQ(flag, 2);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_func_range) {
  {
    std::atomic<int> flag = 0;
    std::array<int, 2> results = {5, 5};
    auto ts = std::ranges::views::iota(0, 2) |
              std::ranges::views::transform([&](int i) {
                return [&results, &flag, i]() {
                  results[i] = i;
                  ++flag;
                  flag.notify_all();
                };
              });
    tmc::post_bulk(ex(), ts, 0);
    flag.wait(0);
    if (flag != 2) {
      flag.wait(1);
    }
    EXPECT_EQ(flag, 2);
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_waitable_coro_begin_count) {
  tmc::post_bulk_waitable(
    ex(), tmc::iter_adapter(0, [](int) -> tmc::task<void> { co_return; }), 10, 0
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

TEST_F(CATEGORY, post_bulk_waitable_coro_begin_end) {
  {
    std::array<int, 2> results = {5, 5};
    auto ts =
      (std::ranges::views::iota(0, 2) |
       std::ranges::views::transform([&results](int i) -> tmc::task<void> {
         return [](int* out, int val) -> tmc::task<void> {
           *out = val;
           co_return;
         }(&results[i], i);
       }));
    tmc::post_bulk_waitable(ex(), ts.begin(), ts.end(), 0).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_waitable_coro_range) {
  {
    std::array<int, 2> results = {5, 5};
    auto ts =
      (std::ranges::views::iota(0, 2) |
       std::ranges::views::transform([&results](int i) -> tmc::task<void> {
         return [](int* out, int val) -> tmc::task<void> {
           *out = val;
           co_return;
         }(&results[i], i);
       }));
    tmc::post_bulk_waitable(ex(), ts, 0).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_waitable_func_begin_count) {
  {
    std::array<int, 2> results = {5, 5};
    auto ts =
      std::ranges::views::iota(0) | std::ranges::views::transform([&](int i) {
        return [&results, i]() { results[i] = i; };
      });
    tmc::post_bulk_waitable(ex(), ts.begin(), 2, 0).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_waitable_func_begin_end) {
  {
    std::array<int, 2> results = {5, 5};
    auto ts = std::ranges::views::iota(0, 2) |
              std::ranges::views::transform([&](int i) {
                return [&results, i]() { results[i] = i; };
              });
    tmc::post_bulk_waitable(ex(), ts.begin(), ts.end(), 0).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_bulk_waitable_func_range) {
  {
    std::array<int, 2> results = {5, 5};
    auto ts = std::ranges::views::iota(0, 2) |
              std::ranges::views::transform([&](int i) {
                return [&results, i]() { results[i] = i; };
              });
    tmc::post_bulk_waitable(ex(), ts, 0).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, async_main) {
  test_async_main(ex(), []() -> tmc::task<void> { co_await empty_task(); }());
  int x = test_async_main_int(ex(), []() -> tmc::task<int> {
    int y = co_await int_task();
    co_return y;
  }());
  EXPECT_EQ(x, 1);
}

TEST_F(CATEGORY, spawn_func) {
  const size_t NTASKS = 10;
  const size_t NCHECKS = 10;
  std::array<size_t, NTASKS * NCHECKS> results;
  auto tasks =
    std::ranges::views::iota(
      static_cast<size_t>(0), static_cast<size_t>(NTASKS)
    ) |
    std::ranges::views::transform([&results](size_t slot) -> tmc::task<void> {
      return [](
               std::array<size_t, NTASKS * NCHECKS>& Results, size_t base
             ) -> tmc::task<void> {
        // These inc() calls are not thread-safe but are synchronized because we
        // co_await throughout
        size_t idx = base;
        inc(Results, idx);
        co_await tmc::spawn_func([&Results, &idx]() { inc(Results, idx); });
        EXPECT_EQ(idx, base + 2);

        idx = co_await tmc::spawn_func([&Results, idx]() mutable {
          inc(Results, idx);
          return idx;
        });
        EXPECT_EQ(idx, base + 3);
        co_await tmc::spawn(
          [](
            std::array<size_t, NTASKS * NCHECKS>& MyResult, size_t& Idx
          ) -> tmc::task<void> {
            inc(MyResult, Idx);
            co_await tmc::yield();
            inc(MyResult, Idx);
          }(Results, idx)
        );
        EXPECT_EQ(idx, base + 5);
        // in this case, the spawned function returns a task,
        // and a 2nd co_await is required
        co_await co_await tmc::spawn_func(
          [&idx, &Results]() -> tmc::task<void> {
            return [](
                     std::array<size_t, NTASKS * NCHECKS>& MyResult, size_t& Idx
                   ) -> tmc::task<void> {
              inc(MyResult, Idx);
              co_await tmc::yield();
              inc(MyResult, Idx);
              co_return;
            }(Results, idx);
          }
        );
        EXPECT_EQ(idx, base + 7);
        {
          auto t = tmc::spawn_func([&Results, &idx]() { inc(Results, idx); });
          co_await std::move(t).run_on(ex()).resume_on(ex()).with_priority(0);
          EXPECT_EQ(idx, base + 8);
        }
        {
          auto t =
            tmc::spawn_func([&Results, &idx]() { inc(Results, idx); }).fork();
          co_await std::move(t);
          EXPECT_EQ(idx, base + 9);
        }
        {
          auto t = tmc::spawn_func([&Results, idx]() mutable {
                     inc(Results, idx);
                     return idx;
                   }).fork();
          idx = co_await std::move(t);
          EXPECT_EQ(idx, base + 10);
        }
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
               std::array<size_t, NTASKS * NCHECKS>& Results, size_t base
             ) -> tmc::task<void> {
        size_t idx = base;
        inc(Results, idx);
        co_await tmc::spawn(inc_task(Results, idx));
        EXPECT_EQ(idx, base + 2);

        auto early = tmc::spawn(inc_task_int(Results, idx)).fork();
        idx = co_await tmc::spawn(inc_task_int(Results, idx + 1));
        auto r = co_await std::move(early);
        EXPECT_EQ(r, base + 3);
        EXPECT_EQ(idx, base + 4);

        auto t = tmc::spawn(inc_task(Results, idx));
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

TEST_F(CATEGORY, spawn_value) {
  auto future = post_waitable(
    ex(),
    []() -> tmc::task<void> {
      int value = co_await spawn([]() -> tmc::task<int> { co_return 1; }());

      auto t = [](int Value) -> tmc::task<int> {
        co_await tmc::yield();
        co_return Value + 1;
      }(value);
      auto s = spawn(std::move(t));
      value = co_await std::move(s);
      EXPECT_EQ(value, 2);

      // in this case, the spawned function returns immediately,
      // and a 2nd co_await is required
      value = co_await co_await tmc::spawn_func([value]() -> tmc::task<int> {
        return [](int Value) -> tmc::task<int> { co_return Value + 1; }(value);
      });
      EXPECT_EQ(value, 3);

      // You can capture an rvalue reference, but not an lvalue reference,
      // to the result of co_await spawn(). The result will be a temporary
      // kept alive by lifetime extension.
      auto spt = spawn([](int InnerSlot) -> tmc::task<int> {
        co_return InnerSlot + 1;
      }(value));
      auto&& sptr = co_await std::move(spt);
      value = sptr;
      EXPECT_EQ(value, 4);
      co_return;
    }(),
    0
  );
  future.wait();
}

TEST_F(CATEGORY, spawn_many_small) {
  auto future = post_waitable(
    ex(),
    []() -> tmc::task<void> {
      int value = 0;
      auto t = [](int Value) -> tmc::task<int> {
        co_await tmc::yield();
        co_return Value + 1;
      }(value);
      std::array<int, 1> result = co_await spawn_many<1>(&t);
      value = result[0];

      auto t2 = [](int& Value) -> tmc::task<void> {
        ++Value;
        co_return;
      }(value);
      co_await spawn_many<1>(&t2);
      EXPECT_EQ(value, 2);

      {
        auto t3 = [](int Value) -> tmc::task<int> {
          co_return Value + 1;
        }(value);
        auto ts = spawn_many<1>(&t3).fork();
        auto results = co_await std::move(ts);
        EXPECT_EQ(results[0], 3);
      }

      auto make_task_array = []() -> std::array<tmc::task<int>, 2> {
        return std::array<tmc::task<int>, 2>{
          []() -> tmc::task<int> { co_return 1; }(),
          [](int i) -> tmc::task<int> { co_return i + 1; }(1)
        };
      };

      {
        auto tasks = make_task_array();
        std::array<int, 2> results = co_await tmc::spawn_many<2>(tasks.begin());
        EXPECT_EQ(results[0], 1);
        EXPECT_EQ(results[1], 2);
      }
      {
        auto tasks = make_task_array();
        std::vector<int> results = co_await tmc::spawn_many(tasks);
        EXPECT_EQ(results[0], 1);
        EXPECT_EQ(results[1], 2);
      }
      {
        auto tasks = make_task_array();
        std::vector<int> results = co_await tmc::spawn_many(tasks.begin(), 2);
        EXPECT_EQ(results.size(), 2);
        EXPECT_EQ(results[0], 1);
        EXPECT_EQ(results[1], 2);
      }
      {
        auto tasks = make_task_array();
        std::vector<int> results =
          co_await tmc::spawn_many(tasks.begin(), tasks.end());
        EXPECT_EQ(results.size(), 2);
        EXPECT_EQ(results[0], 1);
        EXPECT_EQ(results[1], 2);
      }
      co_return;
    }(),
    0
  );
  future.wait();
}

// Coerce a task into a coroutine_handle to erase its promise type
// This will simply behave as if a std::function<void()> was passed.
static inline std::coroutine_handle<>
external_coro_as_std_function_test_task(int I) {
  return [](int) -> tmc::task<void> { co_return; }(I);
}

TEST_F(CATEGORY, external_coro_as_std_function) {
  tmc::post_waitable(
    ex(),
    []() -> tmc::task<void> {
      co_await tmc::spawn_func(external_coro_as_std_function_test_task(4));
    }(),
    0
  )
    .wait();
  tmc::post_waitable(
    ex(),
    []() -> tmc::task<void> {
      co_await tmc::spawn_func_many<2>(
        (std::ranges::views::iota(5) |
         std::ranges::views::transform(external_coro_as_std_function_test_task))
          .begin()
      );
    }(),
    0
  )
    .wait();
}

#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"

#include <gtest/gtest.h>
#include <ranges>

#define CATEGORY test_ex_asio

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::asio_executor().init(); }

  static void TearDownTestSuite() { tmc::asio_executor().teardown(); }

  static tmc::ex_asio& ex() { return tmc::asio_executor(); }
};

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
        inc(results, idx);
        co_await spawn(
          [](
            std::array<size_t, NTASKS * NCHECKS>& results, size_t& idx
          ) -> tmc::task<void> {
            inc(results, idx);
            co_await tmc::yield();
            inc(results, idx);
          }(results, idx)
        );
        EXPECT_EQ(idx, base + 6);
        inc(results, idx);
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
        EXPECT_EQ(idx, base + 9);
        inc(results, idx);
        co_return;
      }(results, slot * NCHECKS);
    });
  auto future = post_bulk_waitable(ex(), tasks.begin(), NTASKS, 0);
  future.get();
  for (size_t i = 0; i < results.size(); ++i) {
    EXPECT_EQ(results[i], i);
  }
}

TEST_F(CATEGORY, run_early) {
  const size_t NTASKS = 10;
  const size_t NCHECKS = 10;
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
        co_await tmc::spawn(inc_task(results, base));
        EXPECT_EQ(idx, base + 2);

        idx = co_await tmc::spawn_func([&results, idx]() mutable {
          inc(results, idx);
          return idx;
        });
        EXPECT_EQ(idx, base + 3);
        inc(results, idx);
        co_await spawn(
          [](
            std::array<size_t, NTASKS * NCHECKS>& results, size_t& idx
          ) -> tmc::task<void> {
            inc(results, idx);
            co_await tmc::yield();
            inc(results, idx);
          }(results, idx)
        );
        EXPECT_EQ(idx, base + 6);
        inc(results, idx);
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
        EXPECT_EQ(idx, base + 9);
        inc(results, idx);
        EXPECT_EQ(idx, base + NCHECKS);
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
      int value = 0;
      std::printf("%d: pre outer\n", value);
      value = co_await [value]() -> tmc::task<int> {
        std::printf("func 0\t");
        co_return value + 1;
      }();
      value += co_await spawn([]() -> tmc::task<int> {
        std::printf("func 1\t");
        co_return 1;
      }());

      auto t = [](int Value) -> tmc::task<int> {
        co_await tmc::yield();
        co_await tmc::yield();
        std::printf("func 2\t");
        co_return Value + 1;
      }(value);
      value = co_await spawn(std::move(t));

      // in this case, the spawned function returns immediately,
      // and a 2nd co_await is required
      value = co_await co_await tmc::spawn_func([value]() -> tmc::task<int> {
        return [](int Value) -> tmc::task<int> {
          co_await tmc::yield();
          co_await tmc::yield();
          std::printf("func 3\t");
          co_return Value + 1;
        }(value);
      });

      // You can capture an rvalue reference, but not an lvalue reference,
      // to the result of co_await spawn(). The result will be a temporary
      // kept alive by lifetime extension.
      auto spt = spawn([](int InnerSlot) -> tmc::task<int> {
        std::printf("func 4\t");
        co_return InnerSlot + 1;
      }(value));
      auto&& sptr = co_await std::move(spt);
      value = sptr;

      if (value != 5) {
        printf("expected 5 but got %d\n", value);
      }
      std::printf("%d: post outer\n", value);
      co_return;
    }(),
    0
  );
  future.wait();
}

TEST_F(CATEGORY, spawn_many) {
  auto future = post_waitable(
    ex(),
    []() -> tmc::task<void> {
      int value = 0;
      std::printf("%d: pre outer\n", value);
      auto t = [](int Slot) -> tmc::task<int> {
        co_await tmc::yield();
        co_await tmc::yield();
        std::printf("func %d\n", Slot);
        co_return Slot + 1;
      }(value);
      auto result = co_await spawn_many<1>(&t);
      value = result[0];
      auto t2 = [](int Value) -> tmc::task<void> {
        co_await tmc::yield();
        co_await tmc::yield();
        std::printf("func %d\n", Value);
      }(value);
      co_await spawn_many<1>(&t2);
      value++;
      auto t3 = [](int Value) -> tmc::task<void> {
        co_await tmc::yield();
        co_await tmc::yield();
        std::printf("func %d\n", Value);
      }(value);
      co_await spawn_many<1>(&t3);
      std::printf("%d: post outer\n", value);
      co_return;
    }(),
    0
  );
  future.wait();
}

// Coerce a task into a coroutine_handle to erase its promise type
// This will simply behave as if a std::function<void()> was passed.
static inline std::coroutine_handle<>
_external_coro_as_std_function_test_task(int I) {
  return [](int i) -> tmc::task<void> {
    std::printf("external_coro_test_task(%d)...\n", i);
    co_return;
  }(I);
}

TEST_F(CATEGORY, external_coro_as_std_function) {
  tmc::post_waitable(
    ex(),
    []() -> tmc::task<void> {
      co_await tmc::spawn_func(_external_coro_as_std_function_test_task(4));
    }(),
    0
  )
    .wait();
  tmc::post_waitable(
    ex(),
    []() -> tmc::task<void> {
      co_await tmc::spawn_func_many<2>(
        (std::ranges::views::iota(5) |
         std::ranges::views::transform(_external_coro_as_std_function_test_task)
        )
          .begin()
      );
    }(),
    0
  )
    .wait();
  // The post() and post_bulk() tasks at the top of this function are
  // detached... wait a bit to let them finish. This isn't safe - you should
  // wait on a future instead. But I explicitly want to demo the use of the
  // non-waitable functions.
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

#include "test_executors.ipp"

#undef CATEGORY
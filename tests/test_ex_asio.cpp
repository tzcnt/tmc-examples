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

// Coerce a task into a coroutine_handle to erase its promise type
// This will simply behave as if a std::function<void()> was passed.
static inline std::coroutine_handle<>
_external_coro_as_std_function_test_task(int I) {
  return [](int i) -> tmc::task<void> { co_return; }(I);
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
}

#include "test_executors.ipp"

#undef CATEGORY
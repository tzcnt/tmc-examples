#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_braid.hpp"

#include <gtest/gtest.h>

template <typename Executor> tmc::task<int> bounce(Executor& Exec) {
  size_t result = 0;
  for (size_t i = 0; i < 100; ++i) {
    auto scope = co_await tmc::enter(Exec);
    ++result;
    co_await scope.exit();
    ++result;
  }
  co_return result;
}

TEST_F(CATEGORY, nested_ex_cpu) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 200);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 200);
  }());
}

TEST_F(CATEGORY, nested_ex_asio) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_asio localEx;
    localEx.init();

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 200);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 200);
  }());
}

TEST_F(CATEGORY, nested_ex_braid) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_braid localEx;

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 200);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 200);
  }());
}

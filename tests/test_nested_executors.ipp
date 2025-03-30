#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_braid.hpp"

#include <gtest/gtest.h>

// All 3 of the tests in this file produce TSan false positives in the same way:
// 1. Inner coro enter()s the nested executor (this is the "racing read" to
// TSan)
// 2. Inner coro exit()s the nested executor
// 3. Inner coro finishes
// 4. Outer coro destroys the nested executor (this is the "racing write" to
// TSan)

// Because the read and write happen on different threads, TSan sees this as a
// race. However, we know that the executor cannot be destroyed before the coro
// has exited.

// I tried a few different ways to counteract this:
// - using __tsan_acquire() / __tsan_release() annotations in enter(), exit()
//   and the executor destructor
// - using __attribute__((no_sanitize("thread")))
// - using the compile-time blacklist
// - using the runtime blacklist
// Unfortunately none of these approaches worked. Perhaps the coroutine
// nature of the functions confuses the name-matching behavior of TSan.
// For now I have simply disabled these tests under TSan.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define TSAN_ENABLED
#endif
#endif

#ifndef TSAN_ENABLED
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

#endif
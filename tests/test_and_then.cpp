#include "test_common.hpp"
#include "tmc/aw_and_then.hpp"
#include "tmc/spawn.hpp"
#include "tmc/task.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_and_then

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

tmc::task<int> async_add(int a, int b) {
  co_return a + b;
}

tmc::task<int> async_multiply(int a) {
  co_return a * 2;
}

tmc::task<void> async_void() {
  co_return;
}

tmc::task<int> async_from_void() {
  co_return 42;
}

TEST_F(CATEGORY, basic_chain) {
  test_async_main(ex(), []() -> tmc::task<void> {
    int result = co_await tmc::and_then(async_add(2, 3), [](int value) {
      return async_multiply(value);
    });
    EXPECT_EQ(result, 10);
  }());
}

TEST_F(CATEGORY, multiple_chains) {
  test_async_main(ex(), []() -> tmc::task<void> {
    int result = co_await tmc::and_then(async_add(1, 2), [](int value) {
                   return async_add(value, 3);
                 }).and_then([](int value) {
      return async_multiply(value);
    });
    EXPECT_EQ(result, 12);
  }());
}

TEST_F(CATEGORY, chain_with_spawn) {
  test_async_main(ex(), []() -> tmc::task<void> {
    int result = co_await tmc::spawn(tmc::and_then(async_add(5, 5), [](int value) {
                           return async_multiply(value);
                         }));
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, chain_void_to_value) {
  test_async_main(ex(), []() -> tmc::task<void> {
    int result = co_await tmc::and_then(async_void(), []() {
      return async_from_void();
    });
    EXPECT_EQ(result, 42);
  }());
}

TEST_F(CATEGORY, chain_value_to_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await tmc::and_then(async_add(10, 20), [](int value) {
      EXPECT_EQ(value, 30);
      return async_void();
    });
  }());
}

TEST_F(CATEGORY, chain_with_customizations) {
  test_async_main(ex(), []() -> tmc::task<void> {
    int result = co_await tmc::and_then(async_add(3, 7), [](int value) {
                           return async_multiply(value);
                         })
                           .run_on(ex())
                           .resume_on(ex())
                           .with_priority(5);
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, long_chain) {
  test_async_main(ex(), []() -> tmc::task<void> {
    int result = co_await tmc::and_then(async_add(1, 1), [](int value) {
                   return async_add(value, 2);
                 })
                   .and_then([](int value) {
                     return async_add(value, 3);
                   })
                   .and_then([](int value) {
                     return async_add(value, 4);
                   })
                   .and_then([](int value) {
                   return async_multiply(value);
                   });
                   EXPECT_EQ(result, 22);
  }());
}

TEST_F(CATEGORY, chain_with_lambda_values) {
  test_async_main(ex(), []() -> tmc::task<void> {
    int result = co_await tmc::and_then(
      async_add(5, 5),
      [](int value) -> tmc::task<int> {
        co_return value + 10;
      }
    );
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, nested_chains) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto inner_chain = tmc::and_then(async_add(2, 3), [](int value) {
      return async_multiply(value);
    });

    int result = co_await tmc::and_then(
      std::move(inner_chain),
      [](int value) -> tmc::task<int> {
        co_return value + 5;
      }
    );
    EXPECT_EQ(result, 15);
  }());
}

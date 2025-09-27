#define TMC_IMPL

/// This is the test for exceptions. It is a separate executable from the main
/// tests because it tests the behavior of exception handling with unknown
/// awaitables. The main tests binary has defined TMC_NO_UNKNOWN_AWAITABLES, as
/// ensuring that all TMC awaitables are well-formed is part of the test
/// coverage.

#include "tmc/all_headers.hpp" // IWYU pragma: keep
#include "tmc/detail/compat.hpp"
#include "tmc/detail/concepts_awaitable.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/spawn.hpp"

#include <coroutine>
#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "test_common.hpp"

#include "gtest/gtest.h"

#if TMC_HAS_EXCEPTIONS

#define CATEGORY exceptions

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

void throws() { throw(std::runtime_error("foo")); }

struct empty {};
template <bool Known>
using KnownTag =
  std::conditional_t<Known, tmc::detail::AwaitTagNoGroupAsIs, empty>;

template <bool Known, size_t ThrowAt>
struct aw_throw_void : public KnownTag<Known> {
  bool await_ready() {
    if constexpr (ThrowAt == 0) {
      throws();
    }
    return false;
  }
  void await_suspend(std::coroutine_handle<> Outer) {
    if constexpr (ThrowAt == 1) {
      throws();
    }
    tmc::detail::post_checked(tmc::current_executor(), std::move(Outer));
  }
  void await_resume() {
    if constexpr (ThrowAt == 2) {
      throws();
    }
  }
};

template <bool Known, size_t ThrowAt>
struct aw_throw_int : public KnownTag<Known> {
  bool await_ready() {
    if constexpr (ThrowAt == 0) {
      throws();
    }
    return false;
  }
  void await_suspend(std::coroutine_handle<> Outer) {
    if constexpr (ThrowAt == 1) {
      throws();
    }
    tmc::detail::post_checked(tmc::current_executor(), std::move(Outer));
  }
  int await_resume() {
    if constexpr (ThrowAt == 2) {
      throws();
    }
    return 5;
  }
};

TEST_F(CATEGORY, throw_catch_known_awaitable_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Known awaitables are awaited directly and their exception can be caught
    bool caught = false;
    try {
      co_await aw_throw_void<true, 0>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      co_await aw_throw_void<true, 1>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      co_await aw_throw_void<true, 2>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);
  }());
}

TEST_F(CATEGORY, throw_catch_unknown_awaitable_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Unknown awaitables are wrapped in tmc::wrapper_task,
    // which stores and rethrows exceptions
    bool caught = false;
    try {
      co_await aw_throw_void<false, 0>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      co_await aw_throw_void<false, 1>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      co_await aw_throw_void<false, 2>{};
    } catch (std::runtime_error ex) {
      caught = true;
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
    }
    EXPECT_TRUE(caught);
  }());
}

TEST_F(CATEGORY, throw_catch_known_awaitable_result) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Known awaitables are awaited directly and their exception can be caught
    [[maybe_unused]] int x;
    bool caught = false;
    try {
      x = co_await aw_throw_int<true, 0>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      x = co_await aw_throw_int<true, 1>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      x = co_await aw_throw_int<true, 2>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);
  }());
}

TEST_F(CATEGORY, throw_catch_unknown_awaitable_result) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Unknown awaitables are wrapped in tmc::wrapper_task,
    // which stores and rethrows exceptions
    [[maybe_unused]] int x;
    bool caught = false;
    caught = false;
    try {
      x = co_await aw_throw_int<false, 0>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      x = co_await aw_throw_int<false, 1>{};
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);

    caught = false;
    try {
      x = co_await aw_throw_int<false, 2>{};
    } catch (std::runtime_error ex) {
      caught = true;
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
    }
    EXPECT_TRUE(caught);
  }());
}

TEST_F(CATEGORY, throw_catch) {
  test_async_main(ex(), []() -> tmc::task<void> {
    try {
      throws();
    } catch (std::runtime_error ex) {
    }
    co_return;
  }());
}

tmc::task<void> throwing_task_void() {
  throw(std::runtime_error("throws_task_void"));
  co_return;
}

tmc::task<int> throwing_task_int() {
  throw(std::runtime_error("throws_task_result"));
  co_return 5;
}

TEST_F(CATEGORY, wrapper_throws_no_await) {
  {
    bool caught = false;
    auto t = []() -> tmc::detail::task_wrapper<void> {
      throws();
      co_return;
    }();
    // task_wrapper.resume() is noexcept - but it's type erased to a
    // coroutine_handle<> before being resumed by the runtime, and
    // coroutine_handle<>'s resume() is not noexcept
    std::coroutine_handle<> tc = std::move(t);
    try {
      tc.resume();
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);
  }
  {
    bool caught = false;
    auto t = []() -> tmc::detail::task_wrapper<int> {
      throws();
      co_return 1;
    }();
    // task_wrapper.resume() is noexcept - but it's type erased to a
    // coroutine_handle<> before being resumed by the runtime, and
    // coroutine_handle<>'s resume() is not noexcept
    std::coroutine_handle<> tc = std::move(t);
    try {
      tc.resume();
    } catch (std::runtime_error ex) {
      EXPECT_EQ(0, strcmp(ex.what(), "foo"));
      caught = true;
    }
    EXPECT_TRUE(caught);
  }
}

struct non_throwing_unknown_default_constructible {
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> Outer) {
    tmc::detail::post_checked(tmc::current_executor(), std::move(Outer));
  }
  int await_resume() { return 5; }
};

TEST_F(CATEGORY, wrapper_no_throw_default_constructible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto x = co_await non_throwing_unknown_default_constructible{};
    EXPECT_EQ(x, 5);
  }());
}

struct no_default_int {
  int a;
  no_default_int(int x) : a{x} {}
};

struct non_throwing_unknown_non_default_constructible {
  bool await_ready() { return false; }
  void await_suspend(std::coroutine_handle<> Outer) {
    tmc::detail::post_checked(tmc::current_executor(), std::move(Outer));
  }
  no_default_int await_resume() { return no_default_int{5}; }
};

TEST_F(CATEGORY, wrapper_no_throw_non_default_constructible) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto x = co_await non_throwing_unknown_non_default_constructible{};
    EXPECT_EQ(x.a, 5);
  }());
}

TEST(exceptions_DeathTest, unhandled_in_main) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        throw std::runtime_error("unhandled_in_main");
        co_return;
      }());
    },
    "unhandled_in_main"
  );
}

TEST(exceptions_DeathTest, unhandled_in_child_void) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        // tmc::task does not have machinery to capture and rethrow exceptions
        // thus, try-catch does not work around a task
        try {
          co_await throwing_task_void();
        } catch (std::exception) {
        }
        co_return;
      }());
    },
    "throws_task_void"
  );
}

TEST(exceptions_DeathTest, unhandled_in_child_result) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        // tmc::task does not have machinery to capture and rethrow exceptions
        // thus, try-catch does not work around a task
        try {
          [[maybe_unused]] auto x = co_await throwing_task_int();
        } catch (std::exception) {
        }
        co_return;
      }());
    },
    "throws_task_result"
  );
}

TEST(exceptions_DeathTest, unhandled_known_awaitable_void) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        co_await aw_throw_void<true, 2>{};
        co_return;
      }());
    },
    "foo"
  );
}

TEST(exceptions_DeathTest, unhandled_known_awaitable_result) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        [[maybe_unused]] auto x = co_await aw_throw_int<true, 2>{};
        co_return;
      }());
    },
    "foo"
  );
}

TEST(exceptions_DeathTest, unhandled_unknown_awaitable_void) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        co_await aw_throw_void<false, 2>{};
        co_return;
      }());
    },
    "foo"
  );
}

TEST(exceptions_DeathTest, unhandled_unknown_awaitable_result) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        [[maybe_unused]] auto x = co_await aw_throw_int<false, 2>{};
        co_return;
      }());
    },
    "foo"
  );
}

TEST(exceptions_DeathTest, spawn_exception) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        bool caught = false;
        try {
          co_await tmc::spawn(aw_throw_void<false, 2>{});
        } catch (std::runtime_error ex) {
          caught = true;
          EXPECT_EQ(0, strcmp(ex.what(), "foo"));
        }
        EXPECT_TRUE(caught);
        co_return;
      }());
    },
    "foo"
  );
}

TEST(exceptions_DeathTest, spawn_tuple_exceptions) {
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      test_async_main(ex, []() -> tmc::task<void> {
        bool caught = false;
        try {
          auto [a, b] = co_await tmc::spawn_tuple(
            aw_throw_void<false, 2>{}, aw_throw_int<false, 2>{}
          );
        } catch (std::runtime_error ex) {
          caught = true;
          EXPECT_EQ(0, strcmp(ex.what(), "foo"));
        }
        EXPECT_TRUE(caught);
        co_return;
      }());
    },
    "foo"
  );
}

#endif

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

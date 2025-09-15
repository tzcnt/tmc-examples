#include "test_common.hpp"
#include "tmc/detail/coro_functor.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_coro_functor

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static int test_coro_functor_free_func_result = 0;
static void free_func() { test_coro_functor_free_func_result = 1; }

TEST_F(CATEGORY, free_function) {
  tmc::coro_functor f(free_func);
  f();
  EXPECT_EQ(test_coro_functor_free_func_result, 1);
  EXPECT_EQ(f.is_coroutine(), false);
}

TEST_F(CATEGORY, lambda) {
  {
    // rvalue
    int result = 0;
    tmc::coro_functor f([&result]() { result = 1; });
    f();
    EXPECT_EQ(result, 1);
    EXPECT_EQ(f.is_coroutine(), false);
  }
  {
    // lvalue
    int result = 0;
    auto inv = [&result]() { result = 1; };
    tmc::coro_functor f(inv);
    f();
    EXPECT_EQ(result, 1);
    EXPECT_EQ(f.is_coroutine(), false);
  }
}

struct inv {
  int* r;
  int* d;
  inv(int& R, int& D) : r(&R), d(&D) {}
  void operator()() {
    EXPECT_NE(r, nullptr);
    *r = 1;
  }

  // If this is copied, d (the destructor count) will be incremented twice.
  inline inv(const inv& Other) : r(Other.r), d(Other.d) {}
  inline inv& operator=(const inv& Other) {
    r = Other.r;
    d = Other.d;
    return *this;
  }

  inline inv(inv&& Other) noexcept : r(Other.r), d(Other.d) {
    Other.r = nullptr;
    Other.d = nullptr;
  }

  inline inv& operator=(inv&& Other) noexcept {
    r = Other.r;
    d = Other.d;
    Other.r = nullptr;
    Other.d = nullptr;
    return *this;
  }
  ~inv() {
    if (d != nullptr) {
      ++(*d);
    }
  }
};

TEST_F(CATEGORY, invokable_struct) {
  {
    // rvalue
    int result = 0;
    int destructor_count = 0;
    {
      tmc::coro_functor f(inv{result, destructor_count});
      EXPECT_EQ(f.is_coroutine(), false);
      f();
    }
    EXPECT_EQ(result, 1);
    EXPECT_EQ(destructor_count, 1);
  }
  {
    // lvalue makes a copy
    int result = 0;
    int destructor_count = 0;
    {
      auto inv_lvalue = inv{result, destructor_count};
      tmc::coro_functor f(inv_lvalue);
      EXPECT_EQ(f.is_coroutine(), false);
      f();
    }
    EXPECT_EQ(result, 1);
    EXPECT_EQ(destructor_count, 2);
  }

  {
    // pointer does not copy
    int result = 0;
    int destructor_count = 0;
    {
      auto inv_lvalue = inv{result, destructor_count};
      tmc::coro_functor f(&inv_lvalue);
      EXPECT_EQ(f.is_coroutine(), false);
      f();
    }
    EXPECT_EQ(result, 1);
    EXPECT_EQ(destructor_count, 1);
  }
}

TEST_F(CATEGORY, move_construct) {
  {
    // rvalue
    int result = 0;
    int destructor_count = 0;
    {
      tmc::coro_functor f(inv{result, destructor_count});
      tmc::coro_functor f2;
      f2 = std::move(f);
      f2();
    }
    EXPECT_EQ(result, 1);
    EXPECT_EQ(destructor_count, 1);
  }
  {
    // lvalue makes a copy
    int result = 0;
    int destructor_count = 0;
    {
      auto inv_lvalue = inv{result, destructor_count};
      tmc::coro_functor f(inv_lvalue);
      tmc::coro_functor f2;
      f2 = std::move(f);
      f2();
    }
    EXPECT_EQ(result, 1);
    EXPECT_EQ(destructor_count, 2);
  }
  {
    // pointer does not copy
    int result = 0;
    int destructor_count = 0;
    {
      auto inv_lvalue = inv{result, destructor_count};
      tmc::coro_functor f(&inv_lvalue);
      tmc::coro_functor f2;
      f2 = std::move(f);
      f2();
    }
    EXPECT_EQ(result, 1);
    EXPECT_EQ(destructor_count, 1);
  }
}

TEST_F(CATEGORY, coro) {
  {
    // rvalue
    int result = 0;
    {
      tmc::coro_functor f([](int& r) -> tmc::task<void> {
        ++r;
        co_return;
      }(result));
      EXPECT_EQ(f.is_coroutine(), true);
      f();
    }
    EXPECT_EQ(result, 1);
  }
  {
    // rvalue does not copy
    int result = 0;
    {
      auto t = [](int& r) -> tmc::task<void> {
        ++r;
        co_return;
      }(result);
      tmc::coro_functor f(std::move(t));
      EXPECT_EQ(f.is_coroutine(), true);
      f();
    }
    EXPECT_EQ(result, 1);
  }
}

TEST_F(CATEGORY, coro_move_construct) {
  {
    // rvalue
    int result = 0;
    {
      tmc::coro_functor f([](int& r) -> tmc::task<void> {
        ++r;
        co_return;
      }(result));
      tmc::coro_functor f2;
      f2 = std::move(f);
      f2();
    }
    EXPECT_EQ(result, 1);
  }
  {
    // rvalue does not copy
    int result = 0;
    {
      auto t = [](int& r) -> tmc::task<void> {
        ++r;
        co_return;
      }(result);
      tmc::coro_functor f(std::move(t));
      tmc::coro_functor f2;
      f2 = std::move(f);
      f2();
    }
    EXPECT_EQ(result, 1);
  }
}

TEST_F(CATEGORY, coro_take) {
  {
    // rvalue
    int result = 0;
    {
      tmc::coro_functor f([](int& r) -> tmc::task<void> {
        ++r;
        co_return;
      }(result));
      EXPECT_EQ(f.is_coroutine(), true);
      auto t2 = f.as_coroutine();
      t2.resume();
    }
    EXPECT_EQ(result, 1);
  }
  {
    // rvalue does not copy
    int result = 0;
    {
      auto t = [](int& r) -> tmc::task<void> {
        ++r;
        co_return;
      }(result);
      tmc::coro_functor f(std::move(t));
      EXPECT_EQ(f.is_coroutine(), true);
      auto t2 = f.as_coroutine();
      t2.resume();
    }
    EXPECT_EQ(result, 1);
  }
}

#undef CATEGORY

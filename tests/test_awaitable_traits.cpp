#include "test_common.hpp"
#include "tmc/util/awaitable_traits.hpp"

#include <gtest/gtest.h>

#include <coroutine>
#include <type_traits>

#define CATEGORY test_awaitable_traits

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

struct NonAwaitableNonCallable {};

struct CallableVoid {
  void operator()() {}
};

struct CallableInt {
  int operator()() { return 42; }
};

struct AwaitableVoid {
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  void await_resume() {}
};

struct AwaitableInt {
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  int await_resume() { return 42; }
};

struct AwaitableAndCallable {
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  int await_resume() { return 42; }
  double operator()() { return 3.14; }
};

/************** IsAwaitable Concept Tests **************/

TEST_F(CATEGORY, is_awaitable_concept_task_void) {
  static_assert(tmc::util::IsAwaitable<tmc::task<void>>);
}

TEST_F(CATEGORY, is_awaitable_concept_task_int) {
  static_assert(tmc::util::IsAwaitable<tmc::task<int>>);
}

TEST_F(CATEGORY, is_awaitable_concept_custom_awaitable) {
  static_assert(tmc::util::IsAwaitable<AwaitableVoid>);
  static_assert(tmc::util::IsAwaitable<AwaitableInt>);
}

TEST_F(CATEGORY, is_awaitable_concept_callable_not_awaitable) {
  static_assert(!tmc::util::IsAwaitable<CallableVoid>);
  static_assert(!tmc::util::IsAwaitable<CallableInt>);
}

TEST_F(CATEGORY, is_awaitable_concept_non_awaitable) {
  static_assert(!tmc::util::IsAwaitable<int>);
  static_assert(!tmc::util::IsAwaitable<void>);
  static_assert(!tmc::util::IsAwaitable<NonAwaitableNonCallable>);
}

/************** is_awaitable_v Tests **************/

TEST_F(CATEGORY, is_awaitable_v_task_void) {
  static_assert(tmc::util::is_awaitable_v<tmc::task<void>>);
}

TEST_F(CATEGORY, is_awaitable_v_task_int) {
  static_assert(tmc::util::is_awaitable_v<tmc::task<int>>);
}

TEST_F(CATEGORY, is_awaitable_v_custom_awaitable) {
  static_assert(tmc::util::is_awaitable_v<AwaitableVoid>);
  static_assert(tmc::util::is_awaitable_v<AwaitableInt>);
}

TEST_F(CATEGORY, is_awaitable_v_callable_not_awaitable) {
  static_assert(!tmc::util::is_awaitable_v<CallableVoid>);
  static_assert(!tmc::util::is_awaitable_v<CallableInt>);
}

TEST_F(CATEGORY, is_awaitable_v_non_awaitable) {
  static_assert(!tmc::util::is_awaitable_v<int>);
  static_assert(!tmc::util::is_awaitable_v<NonAwaitableNonCallable>);
}

/************** awaitable_result_t Tests **************/

TEST_F(CATEGORY, awaitable_result_t_task_void) {
  static_assert(std::is_void_v<tmc::util::awaitable_result_t<tmc::task<void>>>);
}

TEST_F(CATEGORY, awaitable_result_t_task_int) {
  static_assert(
    std::is_same_v<tmc::util::awaitable_result_t<tmc::task<int>>, int>
  );
}

TEST_F(CATEGORY, awaitable_result_t_custom_awaitable) {
  static_assert(std::is_void_v<tmc::util::awaitable_result_t<AwaitableVoid>>);
  static_assert(
    std::is_same_v<tmc::util::awaitable_result_t<AwaitableInt>, int>
  );
}

/************** IsCallableOnly Concept Tests **************/

TEST_F(CATEGORY, is_callable_only_concept_callable) {
  static_assert(tmc::util::IsCallableOnly<CallableVoid>);
  static_assert(tmc::util::IsCallableOnly<CallableInt>);
}

TEST_F(CATEGORY, is_callable_only_concept_awaitable_not_callable_only) {
  static_assert(!tmc::util::IsCallableOnly<tmc::task<void>>);
  static_assert(!tmc::util::IsCallableOnly<AwaitableVoid>);
  static_assert(!tmc::util::IsCallableOnly<AwaitableInt>);
}

TEST_F(CATEGORY, is_callable_only_concept_both_awaitable_and_callable) {
  static_assert(!tmc::util::IsCallableOnly<AwaitableAndCallable>);
}

TEST_F(CATEGORY, is_callable_only_concept_non_callable) {
  static_assert(!tmc::util::IsCallableOnly<int>);
  static_assert(!tmc::util::IsCallableOnly<NonAwaitableNonCallable>);
}

TEST_F(CATEGORY, is_callable_only_concept_lambda) {
  auto lambda_void = []() {};
  auto lambda_int = []() { return 42; };
  static_assert(tmc::util::IsCallableOnly<decltype(lambda_void)>);
  static_assert(tmc::util::IsCallableOnly<decltype(lambda_int)>);
}

/************** is_callable_only_v Tests **************/

TEST_F(CATEGORY, is_callable_only_v_callable) {
  static_assert(tmc::util::is_callable_only_v<CallableVoid>);
  static_assert(tmc::util::is_callable_only_v<CallableInt>);
}

TEST_F(CATEGORY, is_callable_only_v_awaitable_not_callable_only) {
  static_assert(!tmc::util::is_callable_only_v<tmc::task<void>>);
  static_assert(!tmc::util::is_callable_only_v<AwaitableVoid>);
}

TEST_F(CATEGORY, is_callable_only_v_both_awaitable_and_callable) {
  static_assert(!tmc::util::is_callable_only_v<AwaitableAndCallable>);
}

/************** callable_result_t Tests **************/

TEST_F(CATEGORY, callable_result_t_callable_void) {
  static_assert(std::is_void_v<tmc::util::callable_result_t<CallableVoid>>);
}

TEST_F(CATEGORY, callable_result_t_callable_int) {
  static_assert(
    std::is_same_v<tmc::util::callable_result_t<CallableInt>, int>
  );
}

TEST_F(CATEGORY, callable_result_t_lambda) {
  auto lambda_double = []() { return 3.14; };
  static_assert(
    std::is_same_v<tmc::util::callable_result_t<decltype(lambda_double)>, double>
  );
}

/************** executable_kind_v Tests **************/

TEST_F(CATEGORY, executable_kind_v_awaitable) {
  static_assert(
    tmc::util::executable_kind_v<tmc::task<void>> ==
    tmc::util::executable_kind::AWAITABLE
  );
  static_assert(
    tmc::util::executable_kind_v<tmc::task<int>> ==
    tmc::util::executable_kind::AWAITABLE
  );
  static_assert(
    tmc::util::executable_kind_v<AwaitableVoid> ==
    tmc::util::executable_kind::AWAITABLE
  );
  static_assert(
    tmc::util::executable_kind_v<AwaitableInt> ==
    tmc::util::executable_kind::AWAITABLE
  );
}

TEST_F(CATEGORY, executable_kind_v_callable) {
  static_assert(
    tmc::util::executable_kind_v<CallableVoid> ==
    tmc::util::executable_kind::CALLABLE
  );
  static_assert(
    tmc::util::executable_kind_v<CallableInt> ==
    tmc::util::executable_kind::CALLABLE
  );
}

TEST_F(CATEGORY, executable_kind_v_both_is_awaitable) {
  static_assert(
    tmc::util::executable_kind_v<AwaitableAndCallable> ==
    tmc::util::executable_kind::AWAITABLE
  );
}

TEST_F(CATEGORY, executable_kind_v_unknown) {
  static_assert(
    tmc::util::executable_kind_v<int> == tmc::util::executable_kind::UNKNOWN
  );
  static_assert(
    tmc::util::executable_kind_v<NonAwaitableNonCallable> ==
    tmc::util::executable_kind::UNKNOWN
  );
}

/************** executable_result_t Tests **************/

TEST_F(CATEGORY, executable_result_t_awaitable) {
  static_assert(
    std::is_void_v<tmc::util::executable_result_t<tmc::task<void>>>
  );
  static_assert(
    std::is_same_v<tmc::util::executable_result_t<tmc::task<int>>, int>
  );
  static_assert(std::is_void_v<tmc::util::executable_result_t<AwaitableVoid>>);
  static_assert(
    std::is_same_v<tmc::util::executable_result_t<AwaitableInt>, int>
  );
}

TEST_F(CATEGORY, executable_result_t_callable) {
  static_assert(std::is_void_v<tmc::util::executable_result_t<CallableVoid>>);
  static_assert(
    std::is_same_v<tmc::util::executable_result_t<CallableInt>, int>
  );
}

TEST_F(CATEGORY, executable_result_t_both_uses_awaitable_result) {
  static_assert(
    std::is_same_v<tmc::util::executable_result_t<AwaitableAndCallable>, int>
  );
}

TEST_F(CATEGORY, executable_result_t_unknown) {
  static_assert(
    std::is_same_v<tmc::util::executable_result_t<int>, tmc::util::unknown_result>
  );
  static_assert(
    std::is_same_v<
      tmc::util::executable_result_t<NonAwaitableNonCallable>,
      tmc::util::unknown_result
    >
  );
}

/************** executable_traits Tests **************/

TEST_F(CATEGORY, executable_traits_awaitable) {
  using traits_task_void = tmc::util::executable_traits<tmc::task<void>>;
  static_assert(traits_task_void::kind == tmc::util::executable_kind::AWAITABLE);
  static_assert(std::is_void_v<traits_task_void::result_type>);

  using traits_task_int = tmc::util::executable_traits<tmc::task<int>>;
  static_assert(traits_task_int::kind == tmc::util::executable_kind::AWAITABLE);
  static_assert(std::is_same_v<traits_task_int::result_type, int>);

  using traits_awaitable_int = tmc::util::executable_traits<AwaitableInt>;
  static_assert(
    traits_awaitable_int::kind == tmc::util::executable_kind::AWAITABLE
  );
  static_assert(std::is_same_v<traits_awaitable_int::result_type, int>);
}

TEST_F(CATEGORY, executable_traits_callable) {
  using traits_callable_void = tmc::util::executable_traits<CallableVoid>;
  static_assert(
    traits_callable_void::kind == tmc::util::executable_kind::CALLABLE
  );
  static_assert(std::is_void_v<traits_callable_void::result_type>);

  using traits_callable_int = tmc::util::executable_traits<CallableInt>;
  static_assert(
    traits_callable_int::kind == tmc::util::executable_kind::CALLABLE
  );
  static_assert(std::is_same_v<traits_callable_int::result_type, int>);
}

TEST_F(CATEGORY, executable_traits_both_is_awaitable) {
  using traits = tmc::util::executable_traits<AwaitableAndCallable>;
  static_assert(traits::kind == tmc::util::executable_kind::AWAITABLE);
  static_assert(std::is_same_v<traits::result_type, int>);
}

TEST_F(CATEGORY, executable_traits_unknown) {
  using traits_int = tmc::util::executable_traits<int>;
  static_assert(traits_int::kind == tmc::util::executable_kind::UNKNOWN);
  static_assert(
    std::is_same_v<traits_int::result_type, tmc::util::unknown_result>
  );

  using traits_struct = tmc::util::executable_traits<NonAwaitableNonCallable>;
  static_assert(traits_struct::kind == tmc::util::executable_kind::UNKNOWN);
  static_assert(
    std::is_same_v<traits_struct::result_type, tmc::util::unknown_result>
  );
}

/************** unknown_result Type Tests **************/

TEST_F(CATEGORY, unknown_result_type) {
  static_assert(
    std::is_same_v<tmc::util::unknown_result, tmc::detail::not_found>
  );
}

#undef CATEGORY

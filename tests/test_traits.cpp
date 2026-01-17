#include "test_common.hpp"
#include "tmc/detail/task_wrapper.hpp"
#include "tmc/traits.hpp"

#include <gtest/gtest.h>

#include <array>
#include <coroutine>
#include <type_traits>
#include <vector>

#define CATEGORY test_traits

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

/************** is_awaitable Concept Tests **************/

TEST_F(CATEGORY, is_awaitable_task_void) {
  static_assert(tmc::traits::is_awaitable<tmc::task<void>>);
}

TEST_F(CATEGORY, is_awaitable_task_int) {
  static_assert(tmc::traits::is_awaitable<tmc::task<int>>);
}

TEST_F(CATEGORY, is_awaitable_custom_awaitable) {
  static_assert(tmc::traits::is_awaitable<AwaitableVoid>);
  static_assert(tmc::traits::is_awaitable<AwaitableInt>);
}

TEST_F(CATEGORY, is_awaitable_callable_not_awaitable) {
  static_assert(!tmc::traits::is_awaitable<CallableVoid>);
  static_assert(!tmc::traits::is_awaitable<CallableInt>);
}

TEST_F(CATEGORY, is_awaitable_non_awaitable) {
  static_assert(!tmc::traits::is_awaitable<int>);
  static_assert(!tmc::traits::is_awaitable<void>);
  static_assert(!tmc::traits::is_awaitable<NonAwaitableNonCallable>);
}

/************** awaitable_result_t Tests **************/

TEST_F(CATEGORY, awaitable_result_t_task_void) {
  static_assert(
    std::is_void_v<tmc::traits::awaitable_result_t<tmc::task<void>>>
  );
}

TEST_F(CATEGORY, awaitable_result_t_task_int) {
  static_assert(
    std::is_same_v<tmc::traits::awaitable_result_t<tmc::task<int>>, int>
  );
}

TEST_F(CATEGORY, awaitable_result_t_custom_awaitable) {
  static_assert(std::is_void_v<tmc::traits::awaitable_result_t<AwaitableVoid>>);
  static_assert(
    std::is_same_v<tmc::traits::awaitable_result_t<AwaitableInt>, int>
  );
}

TEST_F(CATEGORY, awaitable_result_t_other) {
  static_assert(std::is_same_v<
                tmc::traits::awaitable_result_t<void>, tmc::traits::unknown_t>);
  static_assert(
    std::is_same_v<tmc::traits::awaitable_result_t<int>, tmc::traits::unknown_t>
  );
}

/************** is_callable Concept Tests **************/

TEST_F(CATEGORY, is_callable_callable) {
  static_assert(tmc::traits::is_callable<CallableVoid>);
  static_assert(tmc::traits::is_callable<CallableInt>);
}

TEST_F(CATEGORY, is_callable_awaitable_not_callable_only) {
  static_assert(!tmc::traits::is_callable<tmc::task<void>>);
  static_assert(!tmc::traits::is_callable<AwaitableVoid>);
  static_assert(!tmc::traits::is_callable<AwaitableInt>);
}

TEST_F(CATEGORY, is_callable_both_awaitable_and_callable) {
  static_assert(!tmc::traits::is_callable<AwaitableAndCallable>);
}

TEST_F(CATEGORY, is_callable_non_callable) {
  static_assert(!tmc::traits::is_callable<void>);
  static_assert(!tmc::traits::is_callable<int>);
  static_assert(!tmc::traits::is_callable<NonAwaitableNonCallable>);
}

TEST_F(CATEGORY, is_callable_lambda) {
  auto lambda_void = []() {};
  auto lambda_int = []() { return 42; };
  static_assert(tmc::traits::is_callable<decltype(lambda_void)>);
  static_assert(tmc::traits::is_callable<decltype(lambda_int)>);
}

/************** callable_result_t Tests **************/

TEST_F(CATEGORY, callable_result_t_callable_void) {
  static_assert(std::is_void_v<tmc::traits::callable_result_t<CallableVoid>>);
}

TEST_F(CATEGORY, callable_result_t_callable_int) {
  static_assert(
    std::is_same_v<tmc::traits::callable_result_t<CallableInt>, int>
  );
}

TEST_F(CATEGORY, callable_result_t_lambda) {
  auto lambda_double = []() { return 3.14; };
  static_assert(
    std::is_same_v<
      tmc::traits::callable_result_t<decltype(lambda_double)>, double>
  );
}

/************** executable_kind_v Tests **************/

TEST_F(CATEGORY, executable_kind_v_awaitable) {
  static_assert(
    tmc::traits::executable_kind_v<tmc::task<void>> ==
    tmc::traits::executable_kind::AWAITABLE
  );
  static_assert(
    tmc::traits::executable_kind_v<tmc::task<int>> ==
    tmc::traits::executable_kind::AWAITABLE
  );
  static_assert(
    tmc::traits::executable_kind_v<AwaitableVoid> ==
    tmc::traits::executable_kind::AWAITABLE
  );
  static_assert(
    tmc::traits::executable_kind_v<AwaitableInt> ==
    tmc::traits::executable_kind::AWAITABLE
  );
}

TEST_F(CATEGORY, executable_kind_v_callable) {
  static_assert(
    tmc::traits::executable_kind_v<CallableVoid> ==
    tmc::traits::executable_kind::CALLABLE
  );
  static_assert(
    tmc::traits::executable_kind_v<CallableInt> ==
    tmc::traits::executable_kind::CALLABLE
  );
}

TEST_F(CATEGORY, executable_kind_v_both_is_awaitable) {
  static_assert(
    tmc::traits::executable_kind_v<AwaitableAndCallable> ==
    tmc::traits::executable_kind::AWAITABLE
  );
}

TEST_F(CATEGORY, executable_kind_v_unknown) {
  static_assert(
    tmc::traits::executable_kind_v<void> ==
    tmc::traits::executable_kind::UNKNOWN
  );
  static_assert(
    tmc::traits::executable_kind_v<int> == tmc::traits::executable_kind::UNKNOWN
  );
  static_assert(
    tmc::traits::executable_kind_v<NonAwaitableNonCallable> ==
    tmc::traits::executable_kind::UNKNOWN
  );
}

/************** executable_result_t Tests **************/

TEST_F(CATEGORY, executable_result_t_awaitable) {
  static_assert(
    std::is_void_v<tmc::traits::executable_result_t<tmc::task<void>>>
  );
  static_assert(
    std::is_same_v<tmc::traits::executable_result_t<tmc::task<int>>, int>
  );
  static_assert(
    std::is_void_v<tmc::traits::executable_result_t<AwaitableVoid>>
  );
  static_assert(
    std::is_same_v<tmc::traits::executable_result_t<AwaitableInt>, int>
  );
}

TEST_F(CATEGORY, executable_result_t_callable) {
  static_assert(std::is_void_v<tmc::traits::executable_result_t<CallableVoid>>);
  static_assert(
    std::is_same_v<tmc::traits::executable_result_t<CallableInt>, int>
  );
}

TEST_F(CATEGORY, executable_result_t_both_uses_awaitable_result) {
  static_assert(
    std::is_same_v<tmc::traits::executable_result_t<AwaitableAndCallable>, int>
  );
}

TEST_F(CATEGORY, executable_result_t_unknown) {
  static_assert(
    std::is_same_v<
      tmc::traits::executable_result_t<void>, tmc::traits::unknown_t>
  );
  static_assert(std::is_same_v<
                tmc::traits::executable_result_t<int>, tmc::traits::unknown_t>);
  static_assert(std::is_same_v<
                tmc::traits::executable_result_t<NonAwaitableNonCallable>,
                tmc::traits::unknown_t>);
}

/************** executable_traits Tests **************/

TEST_F(CATEGORY, executable_traits_awaitable) {
  using traits_task_void = tmc::traits::executable_traits<tmc::task<void>>;
  static_assert(
    traits_task_void::kind == tmc::traits::executable_kind::AWAITABLE
  );
  static_assert(std::is_void_v<traits_task_void::result_type>);

  using traits_task_int = tmc::traits::executable_traits<tmc::task<int>>;
  static_assert(
    traits_task_int::kind == tmc::traits::executable_kind::AWAITABLE
  );
  static_assert(std::is_same_v<traits_task_int::result_type, int>);

  using traits_awaitable_int = tmc::traits::executable_traits<AwaitableInt>;
  static_assert(
    traits_awaitable_int::kind == tmc::traits::executable_kind::AWAITABLE
  );
  static_assert(std::is_same_v<traits_awaitable_int::result_type, int>);
}

TEST_F(CATEGORY, executable_traits_callable) {
  using traits_callable_void = tmc::traits::executable_traits<CallableVoid>;
  static_assert(
    traits_callable_void::kind == tmc::traits::executable_kind::CALLABLE
  );
  static_assert(std::is_void_v<traits_callable_void::result_type>);

  using traits_callable_int = tmc::traits::executable_traits<CallableInt>;
  static_assert(
    traits_callable_int::kind == tmc::traits::executable_kind::CALLABLE
  );
  static_assert(std::is_same_v<traits_callable_int::result_type, int>);
}

TEST_F(CATEGORY, executable_traits_both_is_awaitable) {
  using traits = tmc::traits::executable_traits<AwaitableAndCallable>;
  static_assert(traits::kind == tmc::traits::executable_kind::AWAITABLE);
  static_assert(std::is_same_v<traits::result_type, int>);
}

TEST_F(CATEGORY, executable_traits_unknown) {
  using traits_void = tmc::traits::executable_traits<void>;
  static_assert(traits_void::kind == tmc::traits::executable_kind::UNKNOWN);
  static_assert(
    std::is_same_v<traits_void::result_type, tmc::traits::unknown_t>
  );

  using traits_int = tmc::traits::executable_traits<int>;
  static_assert(traits_int::kind == tmc::traits::executable_kind::UNKNOWN);
  static_assert(
    std::is_same_v<traits_int::result_type, tmc::traits::unknown_t>
  );

  using traits_struct = tmc::traits::executable_traits<NonAwaitableNonCallable>;
  static_assert(traits_struct::kind == tmc::traits::executable_kind::UNKNOWN);
  static_assert(
    std::is_same_v<traits_struct::result_type, tmc::traits::unknown_t>
  );
}

/************** Detail Concepts Tests **************/

// Test detail::is_task concept
TEST_F(CATEGORY, detail_is_task) {
  static_assert(tmc::detail::is_task<tmc::task<void>>);
  static_assert(tmc::detail::is_task<tmc::task<int>>);
  static_assert(!tmc::detail::is_task<void>);
  static_assert(!tmc::detail::is_task<int>);
  static_assert(!tmc::detail::is_task<CallableVoid>);
  static_assert(!tmc::detail::is_task<AwaitableVoid>);
  static_assert(!tmc::detail::is_task<NonAwaitableNonCallable>);
}

// Test detail::is_task_void concept
TEST_F(CATEGORY, detail_is_task_void) {
  static_assert(tmc::detail::is_task_void<tmc::task<void>>);
  static_assert(!tmc::detail::is_task_void<tmc::task<int>>);
  static_assert(!tmc::detail::is_task_void<void>);
  static_assert(!tmc::detail::is_task_void<int>);
  static_assert(!tmc::detail::is_task_void<CallableVoid>);
}

// Test detail::is_task_nonvoid concept
TEST_F(CATEGORY, detail_is_task_nonvoid) {
  static_assert(!tmc::detail::is_task_nonvoid<tmc::task<void>>);
  static_assert(tmc::detail::is_task_nonvoid<tmc::task<int>>);
  static_assert(tmc::detail::is_task_nonvoid<tmc::task<double>>);
  static_assert(!tmc::detail::is_task_nonvoid<void>);
  static_assert(!tmc::detail::is_task_nonvoid<int>);
  static_assert(!tmc::detail::is_task_nonvoid<CallableInt>);
}

// Test detail::is_task_result concept
TEST_F(CATEGORY, detail_is_task_result) {
  static_assert(tmc::detail::is_task_result<tmc::task<void>, void>);
  static_assert(tmc::detail::is_task_result<tmc::task<int>, int>);
  static_assert(!tmc::detail::is_task_result<tmc::task<int>, double>);
  static_assert(!tmc::detail::is_task_result<void, int>);
  static_assert(!tmc::detail::is_task_result<int, int>);
  static_assert(!tmc::detail::is_task_result<CallableInt, int>);
}

// Test detail::is_func concept
TEST_F(CATEGORY, detail_is_func) {
  static_assert(tmc::detail::is_func<CallableVoid>);
  static_assert(tmc::detail::is_func<CallableInt>);
  static_assert(!tmc::detail::is_func<tmc::task<void>>);
  static_assert(!tmc::detail::is_func<tmc::task<int>>);
  static_assert(!tmc::detail::is_func<void>);
  static_assert(!tmc::detail::is_func<int>);
  static_assert(!tmc::detail::is_func<NonAwaitableNonCallable>);
}

// Test detail::is_func_void concept
TEST_F(CATEGORY, detail_is_func_void) {
  static_assert(tmc::detail::is_func_void<CallableVoid>);
  static_assert(!tmc::detail::is_func_void<CallableInt>);
  static_assert(!tmc::detail::is_func_void<tmc::task<void>>);
  static_assert(!tmc::detail::is_func_void<void>);
  static_assert(!tmc::detail::is_func_void<int>);
}

// Test detail::is_func_nonvoid concept
TEST_F(CATEGORY, detail_is_func_nonvoid) {
  static_assert(!tmc::detail::is_func_nonvoid<CallableVoid>);
  static_assert(tmc::detail::is_func_nonvoid<CallableInt>);
  static_assert(!tmc::detail::is_func_nonvoid<tmc::task<int>>);
  static_assert(!tmc::detail::is_func_nonvoid<void>);
  static_assert(!tmc::detail::is_func_nonvoid<int>);
  static_assert(!tmc::detail::is_func_nonvoid<NonAwaitableNonCallable>);
}

// Test detail::is_func_result concept
TEST_F(CATEGORY, detail_is_func_result) {
  static_assert(tmc::detail::is_func_result<CallableVoid, void>);
  static_assert(tmc::detail::is_func_result<CallableInt, int>);
  static_assert(!tmc::detail::is_func_result<CallableInt, double>);
  static_assert(!tmc::detail::is_func_result<tmc::task<int>, int>);
  static_assert(!tmc::detail::is_func_result<void, int>);
  static_assert(!tmc::detail::is_func_result<int, int>);
}

// Test detail::task_result_t type trait
TEST_F(CATEGORY, detail_task_result_t) {
  static_assert(std::is_void_v<tmc::detail::task_result_t<tmc::task<void>>>);
  static_assert(
    std::is_same_v<tmc::detail::task_result_t<tmc::task<int>>, int>
  );
  static_assert(
    std::is_same_v<tmc::detail::task_result_t<void>, tmc::detail::unknown_t>
  );
  static_assert(
    std::is_same_v<tmc::detail::task_result_t<int>, tmc::detail::unknown_t>
  );
  static_assert(
    std::is_same_v<
      tmc::detail::task_result_t<CallableInt>, tmc::detail::unknown_t>
  );
}

// Test detail::func_result_t type trait
TEST_F(CATEGORY, detail_func_result_t) {
  static_assert(std::is_void_v<tmc::detail::func_result_t<CallableVoid>>);
  static_assert(std::is_same_v<tmc::detail::func_result_t<CallableInt>, int>);
  static_assert(
    std::is_same_v<tmc::detail::func_result_t<void>, tmc::detail::unknown_t>
  );
  static_assert(
    std::is_same_v<tmc::detail::func_result_t<int>, tmc::detail::unknown_t>
  );
  static_assert(std::is_same_v<
                tmc::detail::func_result_t<NonAwaitableNonCallable>,
                tmc::detail::unknown_t>);
}

// Test detail::HasTaskResult concept
TEST_F(CATEGORY, detail_HasTaskResult) {
  static_assert(tmc::detail::HasTaskResult<tmc::task<void>>);
  static_assert(tmc::detail::HasTaskResult<tmc::task<int>>);
  static_assert(!tmc::detail::HasTaskResult<void>);
  static_assert(!tmc::detail::HasTaskResult<int>);
  static_assert(!tmc::detail::HasTaskResult<CallableVoid>);
  static_assert(!tmc::detail::HasTaskResult<NonAwaitableNonCallable>);
}

// Test detail::HasFuncResult concept
TEST_F(CATEGORY, detail_HasFuncResult) {
  static_assert(tmc::detail::HasFuncResult<CallableVoid>);
  static_assert(tmc::detail::HasFuncResult<CallableInt>);
  static_assert(!tmc::detail::HasFuncResult<void>);
  static_assert(!tmc::detail::HasFuncResult<int>);
  static_assert(!tmc::detail::HasFuncResult<NonAwaitableNonCallable>);
}

// Test detail::is_awaitable concept (underlying implementation)
TEST_F(CATEGORY, detail_is_awaitable) {
  static_assert(tmc::detail::is_awaitable<tmc::task<void>>);
  static_assert(tmc::detail::is_awaitable<tmc::task<int>>);
  static_assert(tmc::detail::is_awaitable<AwaitableVoid>);
  static_assert(tmc::detail::is_awaitable<AwaitableInt>);
  static_assert(!tmc::detail::is_awaitable<void>);
  static_assert(!tmc::detail::is_awaitable<int>);
  static_assert(!tmc::detail::is_awaitable<CallableVoid>);
  static_assert(!tmc::detail::is_awaitable<NonAwaitableNonCallable>);
}

// Test detail::is_callable concept (underlying implementation)
TEST_F(CATEGORY, detail_is_callable) {
  static_assert(tmc::detail::is_callable<CallableVoid>);
  static_assert(tmc::detail::is_callable<CallableInt>);
  static_assert(!tmc::detail::is_callable<tmc::task<void>>);
  static_assert(!tmc::detail::is_callable<AwaitableVoid>);
  static_assert(!tmc::detail::is_callable<AwaitableAndCallable>);
  static_assert(!tmc::detail::is_callable<void>);
  static_assert(!tmc::detail::is_callable<int>);
  static_assert(!tmc::detail::is_callable<NonAwaitableNonCallable>);
}

// Test detail::awaitable_result_t (underlying implementation)
TEST_F(CATEGORY, detail_awaitable_result_t) {
  static_assert(
    std::is_void_v<tmc::detail::awaitable_result_t<tmc::task<void>>>
  );
  static_assert(
    std::is_same_v<tmc::detail::awaitable_result_t<tmc::task<int>>, int>
  );
  static_assert(std::is_same_v<
                tmc::detail::awaitable_result_t<void>, tmc::detail::unknown_t>);
  static_assert(
    std::is_same_v<tmc::detail::awaitable_result_t<int>, tmc::detail::unknown_t>
  );
  static_assert(
    std::is_same_v<tmc::detail::awaitable_result_t<tmc::task<int>>, int>
  );
  static_assert(std::is_void_v<tmc::detail::awaitable_result_t<AwaitableVoid>>);
  static_assert(
    std::is_same_v<tmc::detail::awaitable_result_t<AwaitableInt>, int>
  );
}

// Test detail::executable_kind_v (underlying implementation)
TEST_F(CATEGORY, detail_executable_kind_v) {
  static_assert(
    tmc::detail::executable_kind_v<tmc::task<void>> ==
    tmc::detail::executable_kind::AWAITABLE
  );
  static_assert(
    tmc::detail::executable_kind_v<CallableVoid> ==
    tmc::detail::executable_kind::CALLABLE
  );
  static_assert(
    tmc::detail::executable_kind_v<void> ==
    tmc::detail::executable_kind::UNKNOWN
  );
  static_assert(
    tmc::detail::executable_kind_v<int> == tmc::detail::executable_kind::UNKNOWN
  );
}

// Test detail::executable_result_t (underlying implementation)
TEST_F(CATEGORY, detail_executable_result_t) {
  static_assert(
    std::is_void_v<tmc::detail::executable_result_t<tmc::task<void>>>
  );
  static_assert(
    std::is_same_v<tmc::detail::executable_result_t<CallableInt>, int>
  );
  static_assert(
    std::is_same_v<
      tmc::detail::executable_result_t<void>, tmc::detail::unknown_t>
  );
  static_assert(std::is_same_v<
                tmc::detail::executable_result_t<int>, tmc::detail::unknown_t>);
}

// Test detail::executable_traits (underlying implementation)
TEST_F(CATEGORY, detail_executable_traits) {
  using traits_task = tmc::detail::executable_traits<tmc::task<int>>;
  static_assert(traits_task::kind == tmc::detail::executable_kind::AWAITABLE);
  static_assert(std::is_same_v<traits_task::result_type, int>);

  using traits_callable = tmc::detail::executable_traits<CallableInt>;
  static_assert(
    traits_callable::kind == tmc::detail::executable_kind::CALLABLE
  );
  static_assert(std::is_same_v<traits_callable::result_type, int>);

  using traits_void = tmc::detail::executable_traits<void>;
  static_assert(traits_void::kind == tmc::detail::executable_kind::UNKNOWN);
  static_assert(
    std::is_same_v<traits_void::result_type, tmc::detail::unknown_t>
  );

  using traits_unknown = tmc::detail::executable_traits<int>;
  static_assert(traits_unknown::kind == tmc::detail::executable_kind::UNKNOWN);
  static_assert(
    std::is_same_v<traits_unknown::result_type, tmc::detail::unknown_t>
  );
}

/************** Awaitable Traits Tag Tests **************/

struct TaggedAwaitableAsIs : tmc::detail::AwaitTagNoGroupAsIs {
  bool await_ready() { return true; }
  void await_suspend(std::coroutine_handle<>) {}
  int await_resume() { return 42; }
};

struct TaggedAwaitableCoAwait : tmc::detail::AwaitTagNoGroupCoAwait {
  struct Awaiter {
    bool await_ready() { return true; }
    void await_suspend(std::coroutine_handle<>) {}
    double await_resume() { return 3.14; }
  };
  Awaiter operator co_await() && { return Awaiter{}; }
};

struct TaggedAwaitableCoAwaitLvalue
    : tmc::detail::AwaitTagNoGroupCoAwaitLvalue {
  struct Awaiter {
    bool await_ready() { return true; }
    void await_suspend(std::coroutine_handle<>) {}
    float await_resume() { return 1.5f; }
  };
  Awaiter operator co_await() & { return Awaiter{}; }
};

TEST_F(CATEGORY, detail_HasAwaitTagNoGroupAsIs) {
  static_assert(tmc::detail::HasAwaitTagNoGroupAsIs<TaggedAwaitableAsIs>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupAsIs<AwaitableVoid>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupAsIs<void>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupAsIs<int>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupAsIs<TaggedAwaitableCoAwait>);
  static_assert(
    tmc::detail::awaitable_traits<TaggedAwaitableAsIs>::mode ==
    tmc::detail::configure_mode::WRAPPER
  );
  using awaiter_t =
    decltype(tmc::detail::awaitable_traits<TaggedAwaitableAsIs>::get_awaiter(
      std::declval<TaggedAwaitableAsIs>()
    ));
  static_assert(std::is_same_v<awaiter_t, TaggedAwaitableAsIs&&>);
}

TEST_F(CATEGORY, detail_HasAwaitTagNoGroupCoAwait) {
  static_assert(tmc::detail::HasAwaitTagNoGroupCoAwait<TaggedAwaitableCoAwait>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupCoAwait<AwaitableVoid>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupCoAwait<void>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupCoAwait<int>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupCoAwait<TaggedAwaitableAsIs>);
  static_assert(
    tmc::detail::awaitable_traits<TaggedAwaitableCoAwait>::mode ==
    tmc::detail::configure_mode::WRAPPER
  );
  using awaiter_t =
    decltype(tmc::detail::awaitable_traits<TaggedAwaitableCoAwait>::get_awaiter(
      std::declval<TaggedAwaitableCoAwait>()
    ));
  static_assert(std::is_same_v<awaiter_t, TaggedAwaitableCoAwait::Awaiter>);
}

TEST_F(CATEGORY, detail_HasAwaitTagNoGroupCoAwaitLvalue) {
  static_assert(
    tmc::detail::HasAwaitTagNoGroupCoAwaitLvalue<TaggedAwaitableCoAwaitLvalue>
  );
  static_assert(!tmc::detail::HasAwaitTagNoGroupCoAwaitLvalue<AwaitableVoid>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupCoAwaitLvalue<void>);
  static_assert(!tmc::detail::HasAwaitTagNoGroupCoAwaitLvalue<int>);
  static_assert(
    !tmc::detail::HasAwaitTagNoGroupCoAwaitLvalue<TaggedAwaitableCoAwait>
  );
  static_assert(
    tmc::detail::awaitable_traits<TaggedAwaitableCoAwaitLvalue>::mode ==
    tmc::detail::configure_mode::WRAPPER
  );
  using awaiter_t =
    decltype(tmc::detail::awaitable_traits<TaggedAwaitableCoAwaitLvalue>::
               get_awaiter(std::declval<TaggedAwaitableCoAwaitLvalue&>()));
  static_assert(
    std::is_same_v<awaiter_t, TaggedAwaitableCoAwaitLvalue::Awaiter>
  );
}

TEST_F(CATEGORY, tagged_awaitable_result_types) {
  static_assert(tmc::traits::is_awaitable<TaggedAwaitableAsIs>);
  static_assert(
    std::is_same_v<tmc::traits::awaitable_result_t<TaggedAwaitableAsIs>, int>
  );
  static_assert(tmc::detail::is_known_awaitable<TaggedAwaitableAsIs>);

  static_assert(tmc::traits::is_awaitable<TaggedAwaitableCoAwait>);
  static_assert(
    std::is_same_v<
      tmc::traits::awaitable_result_t<TaggedAwaitableCoAwait>, double>
  );
  static_assert(tmc::detail::is_known_awaitable<TaggedAwaitableCoAwait>);

  static_assert(tmc::traits::is_awaitable<TaggedAwaitableCoAwaitLvalue>);
  static_assert(
    std::is_same_v<
      tmc::traits::awaitable_result_t<TaggedAwaitableCoAwaitLvalue>, float>
  );
  static_assert(tmc::detail::is_known_awaitable<TaggedAwaitableCoAwaitLvalue>);
}

/************** IsRange Concept Tests **************/

TEST_F(CATEGORY, detail_IsRange) {
  static_assert(tmc::detail::IsRange<std::vector<int>>);
  static_assert(tmc::detail::IsRange<std::array<int, 5>>);
  static_assert(!tmc::detail::IsRange<void>);
  static_assert(!tmc::detail::IsRange<int>);
  static_assert(!tmc::detail::IsRange<CallableVoid>);
  static_assert(!tmc::detail::IsRange<NonAwaitableNonCallable>);
}

TEST_F(CATEGORY, detail_range_iter) {
  static_assert(std::is_same_v<
                typename tmc::detail::range_iter<std::vector<int>>::type,
                std::vector<int>::iterator>);
  static_assert(std::is_same_v<
                typename tmc::detail::range_iter<std::array<int, 5>>::type,
                std::array<int, 5>::iterator>);
}

/************** result_storage_t Tests **************/

struct NonDefaultConstructible {
  NonDefaultConstructible(int) {}
};

TEST_F(CATEGORY, detail_result_storage_t) {
  static_assert(std::is_same_v<tmc::detail::result_storage_t<int>, int>);
  static_assert(
    std::is_same_v<tmc::detail::result_storage_t<std::string>, std::string>
  );
  static_assert(std::is_same_v<
                tmc::detail::result_storage_t<NonDefaultConstructible>,
                std::optional<NonDefaultConstructible>>);
}

/************** forward_awaitable Tests **************/

struct Movable {};

struct NonMovable {
  NonMovable() {}
  NonMovable(NonMovable&&) = delete;
  NonMovable& operator=(NonMovable&&) = delete;
};

TEST_F(CATEGORY, detail_forward_awaitable) {

  static_assert(std::is_same_v<tmc::detail::forward_awaitable<int>, int>);
  static_assert(std::is_same_v<tmc::detail::forward_awaitable<int&>, int&>);
  static_assert(
    std::is_same_v<tmc::detail::forward_awaitable<const int&>, const int&>
  );

  static_assert(
    std::is_same_v<tmc::detail::forward_awaitable<Movable&>, Movable&>
  );
  static_assert(
    std::is_same_v<tmc::detail::forward_awaitable<Movable&&>, Movable>
  );

  static_assert(
    std::is_same_v<tmc::detail::forward_awaitable<NonMovable&>, NonMovable&>
  );
  // If a type cannot be moved then its rvalue category must be preserved
  static_assert(
    std::is_same_v<tmc::detail::forward_awaitable<NonMovable&&>, NonMovable&&>
  );
}

/************** AwaitResumeIsWellFormed Concept Tests **************/

TEST_F(CATEGORY, detail_AwaitResumeIsWellFormed) {
  static_assert(tmc::detail::AwaitResumeIsWellFormed<AwaitableVoid>);
  static_assert(tmc::detail::AwaitResumeIsWellFormed<AwaitableInt>);
  static_assert(!tmc::detail::AwaitResumeIsWellFormed<void>);
  static_assert(!tmc::detail::AwaitResumeIsWellFormed<int>);
  static_assert(!tmc::detail::AwaitResumeIsWellFormed<CallableVoid>);
  static_assert(!tmc::detail::AwaitResumeIsWellFormed<NonAwaitableNonCallable>);

  static_assert(
    std::is_same_v<
      tmc::detail::unknown_t, tmc::detail::awaitable_traits<int>::result_type>
  );
  static_assert(
    std::is_same_v<
      tmc::detail::unknown_t, tmc::detail::awaitable_traits<void>::result_type>
  );
}

/************** unknown_awaitable_traits Concept Tests **************/

TEST_F(CATEGORY, detail_unknown_awaitable_traits) {
  static_assert(!tmc::detail::is_known_awaitable<void>);
  static_assert(!tmc::detail::is_known_awaitable<int>);
  static_assert(!tmc::detail::is_known_awaitable<AwaitableVoid>);
  static_assert(!tmc::detail::is_known_awaitable<AwaitableInt>);

  using awaiter_void_t =
    decltype(tmc::detail::safe_wrap(std::declval<AwaitableVoid>()));
  static_assert(
    std::is_same_v<awaiter_void_t, tmc::detail::task_wrapper<void>>
  );
  using awaiter_int_t =
    decltype(tmc::detail::safe_wrap(std::declval<AwaitableInt>()));
  static_assert(std::is_same_v<awaiter_int_t, tmc::detail::task_wrapper<int>>);
}

/************** awaitable_traits mode Tests **************/

TEST_F(CATEGORY, detail_awaitable_traits_mode) {
  static_assert(
    tmc::detail::awaitable_traits<AwaitableVoid>::mode ==
    tmc::detail::configure_mode::WRAPPER
  );
  static_assert(
    tmc::detail::awaitable_traits<AwaitableInt>::mode ==
    tmc::detail::configure_mode::WRAPPER
  );
  static_assert(
    tmc::detail::awaitable_traits<void>::mode ==
    tmc::detail::configure_mode::UNKNOWN
  );
  static_assert(
    tmc::detail::awaitable_traits<int>::mode ==
    tmc::detail::configure_mode::UNKNOWN
  );
  static_assert(
    tmc::detail::awaitable_traits<CallableVoid>::mode ==
    tmc::detail::configure_mode::UNKNOWN
  );
  static_assert(
    tmc::detail::awaitable_traits<NonAwaitableNonCallable>::mode ==
    tmc::detail::configure_mode::UNKNOWN
  );
}

TEST_F(CATEGORY, is_task) {
  static_assert(tmc::traits::is_task<tmc::task<void>>);
  static_assert(tmc::traits::is_task<tmc::task<int>>);
  static_assert(!tmc::traits::is_task<void>);
  static_assert(!tmc::traits::is_task<int>);
  static_assert(!tmc::traits::is_task<CallableVoid>);
  static_assert(!tmc::traits::is_task<AwaitableVoid>);
  static_assert(!tmc::traits::is_task<NonAwaitableNonCallable>);
}

TEST_F(CATEGORY, is_task_void) {
  static_assert(tmc::traits::is_task_void<tmc::task<void>>);
  static_assert(!tmc::traits::is_task_void<tmc::task<int>>);
  static_assert(!tmc::traits::is_task_void<void>);
  static_assert(!tmc::traits::is_task_void<int>);
  static_assert(!tmc::traits::is_task_void<CallableVoid>);
}

TEST_F(CATEGORY, is_task_nonvoid) {
  static_assert(!tmc::traits::is_task_nonvoid<tmc::task<void>>);
  static_assert(tmc::traits::is_task_nonvoid<tmc::task<int>>);
  static_assert(tmc::traits::is_task_nonvoid<tmc::task<double>>);
  static_assert(!tmc::traits::is_task_nonvoid<void>);
  static_assert(!tmc::traits::is_task_nonvoid<int>);
  static_assert(!tmc::traits::is_task_nonvoid<CallableInt>);
}

TEST_F(CATEGORY, is_task_result) {
  static_assert(tmc::traits::is_task_result<tmc::task<void>, void>);
  static_assert(tmc::traits::is_task_result<tmc::task<int>, int>);
  static_assert(!tmc::traits::is_task_result<tmc::task<int>, double>);
  static_assert(!tmc::traits::is_task_result<void, int>);
  static_assert(!tmc::traits::is_task_result<int, int>);
  static_assert(!tmc::traits::is_task_result<CallableInt, int>);
}

TEST_F(CATEGORY, is_func) {
  static_assert(tmc::traits::is_func<CallableVoid>);
  static_assert(tmc::traits::is_func<CallableInt>);
  static_assert(!tmc::traits::is_func<tmc::task<void>>);
  static_assert(!tmc::traits::is_func<tmc::task<int>>);
  static_assert(!tmc::traits::is_func<void>);
  static_assert(!tmc::traits::is_func<int>);
  static_assert(!tmc::traits::is_func<NonAwaitableNonCallable>);
}

TEST_F(CATEGORY, is_func_void) {
  static_assert(tmc::traits::is_func_void<CallableVoid>);
  static_assert(!tmc::traits::is_func_void<CallableInt>);
  static_assert(!tmc::traits::is_func_void<tmc::task<void>>);
  static_assert(!tmc::traits::is_func_void<void>);
  static_assert(!tmc::traits::is_func_void<int>);
}

TEST_F(CATEGORY, is_func_nonvoid) {
  static_assert(!tmc::traits::is_func_nonvoid<CallableVoid>);
  static_assert(tmc::traits::is_func_nonvoid<CallableInt>);
  static_assert(!tmc::traits::is_func_nonvoid<tmc::task<int>>);
  static_assert(!tmc::traits::is_func_nonvoid<void>);
  static_assert(!tmc::traits::is_func_nonvoid<int>);
  static_assert(!tmc::traits::is_func_nonvoid<NonAwaitableNonCallable>);
}

TEST_F(CATEGORY, is_func_result) {
  static_assert(tmc::traits::is_func_result<CallableVoid, void>);
  static_assert(tmc::traits::is_func_result<CallableInt, int>);
  static_assert(!tmc::traits::is_func_result<CallableInt, double>);
  static_assert(!tmc::traits::is_func_result<tmc::task<int>, int>);
  static_assert(!tmc::traits::is_func_result<void, int>);
  static_assert(!tmc::traits::is_func_result<int, int>);
}

#undef CATEGORY

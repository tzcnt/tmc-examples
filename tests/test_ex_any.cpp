#include "test_common.hpp"

#include <array>
#include <gtest/gtest.h>
#include <ostream>
#include <ranges>

#define CATEGORY test_ex_any

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_any& ex() { return *tmc::cpu_executor().type_erased(); }
};

template <typename Exec> void test_post(Exec e) {
  tmc::post_waitable(e, []() -> tmc::task<void> { co_return; }(), 0).get();
  tmc::post_waitable(e, empty_task(), 0).get();

  auto x =
    tmc::post_waitable(e, []() -> tmc::task<int> { co_return 1; }(), 0).get();
  EXPECT_EQ(x, 1);
  auto y = tmc::post_waitable(e, int_task(), 0).get();
  EXPECT_EQ(y, 1);
}

template <typename Exec> void test_post_bulk_coro(Exec e) {
  tmc::post_bulk_waitable(
    e, tmc::iter_adapter(0, [](int) -> tmc::task<void> { co_return; }), 10, 0
  )
    .get();

  {
    std::array<int, 2> results = {5, 5};
    tmc::post_bulk_waitable(
      e,
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

template <typename Exec> void test_post_bulk_work_item(Exec e) {
  {
    std::array<tmc::work_item, 2> tasks;
    tasks[0] = []() -> tmc::task<void> { co_return; }();
    tasks[1] = []() -> tmc::task<void> { co_return; }();
    tmc::post_bulk_waitable(e, tasks.data(), 2).get();
  }

  {
    std::array<int, 2> results = {5, 5};
    std::array<tmc::work_item, 2> tasks;
    tasks[0] = [](int* out, int val) -> tmc::task<void> {
      *out = val;
      co_return;
    }(&results[0], 0);
    tasks[1] = [](int* out, int val) -> tmc::task<void> {
      *out = val;
      co_return;
    }(&results[1], 1);
    tmc::post_bulk_waitable(e, tasks.data(), 2).wait();
    EXPECT_EQ(results[0], 0);
    EXPECT_EQ(results[1], 1);
  }
}

TEST_F(CATEGORY, post_ptr) { test_post(&ex()); }

TEST_F(CATEGORY, post_ref) { test_post(ex()); }

TEST_F(CATEGORY, post_bulk_coro_ptr) { test_post_bulk_coro(&ex()); }

TEST_F(CATEGORY, post_bulk_coro_ref) { test_post_bulk_coro(ex()); }

TEST_F(CATEGORY, post_bulk_work_item_ptr) { test_post_bulk_work_item(&ex()); }

TEST_F(CATEGORY, post_bulk_work_item_ref) { test_post_bulk_work_item(ex()); }

// Pointer version of task_enter_context is currently unused, because
// tmc::enter() always delegates to the reference version.
// Add a special test for it here.
TEST_F(CATEGORY, task_enter_context_ptr) {
  auto entry = tmc::detail::executor_traits<tmc::ex_any*>::task_enter_context(
    &ex(), []() -> tmc::task<void> { co_return; }(), 0
  );
  EXPECT_EQ(entry, std::noop_coroutine());
}

#include "test_executors.ipp"

#undef CATEGORY

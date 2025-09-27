// Various tests to increase code coverage in specific areas that are otherwise
// not exercised.

#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/detail/concepts_awaitable.hpp"
#include "tmc/detail/qu_inbox.hpp"
#include "tmc/external.hpp"

#include <gtest/gtest.h>

#include <atomic>

#define CATEGORY test_misc

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, post_checked_default_executor) {
  tmc::set_default_executor(ex());

  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    tmc::detail::post_checked(
      nullptr,
      [](atomic_awaitable<int>& AA) -> tmc::task<void> {
        AA.inc();
        co_return;
      }(aa)
    );
    co_await aa;
  }());

  tmc::set_default_executor(nullptr);
}

TEST_F(CATEGORY, post_bulk_checked_default_executor) {
  tmc::set_default_executor(ex());

  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(2);
    std::array<tmc::work_item, 2> tasks;
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = [](atomic_awaitable<int>& AA) -> tmc::task<void> {
        AA.inc();
        co_return;
      }(aa);
    }
    tmc::detail::post_bulk_checked(nullptr, tasks.data(), tasks.size());
    co_await aa;
  }());

  tmc::set_default_executor(nullptr);
}

TEST_F(CATEGORY, tiny_vec_resize_zero) {
  std::atomic<size_t> count;
  tmc::detail::tiny_vec<destructor_counter> tv;
  tv.resize(2);
  tv.emplace_at(0, &count);
  tv.emplace_at(1, &count);
  tv.resize(0);
  EXPECT_EQ(count.load(), 2);
}

TEST_F(CATEGORY, ex_any_default_constructor) { tmc::ex_any e; }

TEST_F(CATEGORY, qu_inbox_full) {
  tmc::detail::qu_inbox<int, 4> q;
  std::array<int, 5> vs{0, 1, 2, 3, 4};
  auto count = q.try_push_bulk(vs.data(), 5, 2);
  EXPECT_EQ(count, 4);
  EXPECT_EQ(q.try_push(vs[4], 0), false);
  int v;
  size_t prio = 0;
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 0);
  EXPECT_EQ(prio, 2);
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 1);
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 2);
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 3);
  EXPECT_EQ(q.try_pull(v, prio), false);
  EXPECT_EQ(q.try_push(vs[4], 3), true);
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 4);
  EXPECT_EQ(prio, 3);
  EXPECT_EQ(q.try_pull(v, prio), false);
}

TEST_F(CATEGORY, qu_inbox_exact) {
  tmc::detail::qu_inbox<int, 4> q;
  std::array<int, 5> vs{0, 1, 2, 3, 4};
  auto count = q.try_push_bulk(vs.data(), 4, 0);
  EXPECT_EQ(count, 4);
  EXPECT_EQ(q.try_push(vs[4], 0), false);
  int v;
  size_t prio = 0;
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 0);
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 1);
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 2);
  EXPECT_EQ(q.try_pull(v, prio), true);
  EXPECT_EQ(v, 3);
  EXPECT_EQ(q.try_pull(v, prio), false);
}

struct unk_awaitable {
  inline bool await_ready() { return false; }
  inline void await_suspend() {}
  inline void await_resume() {}
};

struct unk_co_await_member {
  inline unk_awaitable operator co_await() { return unk_awaitable{}; }
};

struct unk_co_await_free {};

inline unk_awaitable operator co_await(unk_co_await_free&& f) {
  return unk_awaitable{};
}

// unknown_awaitable_traits::guess_awaiter is normally unevaluated, and is used
// only to get the correct awaitable type here it is evaluated for test
// coverage, and to ensure that it returns the correct awaitable type
TEST_F(CATEGORY, unknown_awaitable_traits) {
  {
    unk_co_await_member a;
    [[maybe_unused]] unk_awaitable x =
      tmc::detail::unknown_awaitable_traits<unk_co_await_member>::guess_awaiter(
        a
      );
  }
  {
    unk_co_await_free b;
    [[maybe_unused]] unk_awaitable y =
      tmc::detail::unknown_awaitable_traits<unk_co_await_free>::guess_awaiter(
        std::move(b)
      );
  }
  {
    unk_awaitable c;
    [[maybe_unused]] unk_awaitable z =
      tmc::detail::unknown_awaitable_traits<unk_awaitable>::guess_awaiter(c);
  }
}

#undef CATEGORY

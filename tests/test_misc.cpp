// Various tests to increase code coverage in specific areas that are otherwise
// not exercised.

#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/external.hpp"
#include "tmc/sync.hpp"

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

TEST_F(CATEGORY, qu_lockfree_expand_implicit_producer_index) {
  auto t1 = tmc::post_bulk_waitable(
    ex(), tmc::iter_adapter(0, [](int i) -> tmc::task<void> { co_return; }),
    8000
  );

  auto t2 = tmc::post_bulk_waitable(
    ex(), tmc::iter_adapter(0, [](int i) -> tmc::task<void> { co_return; }),
    32000
  );
  t1.wait();
  t2.wait();
}

TEST_F(CATEGORY, qu_lockfree_expand_explicit_producer_index) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto t1 =
      tmc::spawn_many(
        tmc::iter_adapter(0, [](int i) -> tmc::task<void> { co_return; }), 8000
      )
        .fork();
    co_await tmc::spawn_many(
      tmc::iter_adapter(0, [](int i) -> tmc::task<void> { co_return; }), 32000
    );
    co_await std::move(t1);
  }());
}

TEST_F(CATEGORY, qu_inbox_try_push_bulk) {
  auto t1 = tmc::post_bulk_waitable(
    ex(), tmc::iter_adapter(0, [](int i) -> tmc::task<void> { co_return; }),
    32000, 0, 0
  );
  t1.wait();
}

TEST_F(CATEGORY, qu_inbox_try_push_full) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(0, 32000);
    for (size_t i = 0; i < 32000; ++i) {
      tmc::post(
        ex(),
        [](atomic_awaitable<int>& AA) -> tmc::task<void> {
          ++AA.ref();
          AA.ref().notify_all();
          co_return;
        }(aa),
        0, 0
      );
    }
    co_await aa;
  }());
}

TEST_F(CATEGORY, post_checked_default_executor) {
  tmc::set_default_executor(ex());

  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(0, 1);
    tmc::detail::post_checked(
      nullptr,
      [](atomic_awaitable<int>& AA) -> tmc::task<void> {
        ++AA.ref();
        AA.ref().notify_all();
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
    atomic_awaitable<int> aa(0, 2);
    std::array<tmc::work_item, 2> tasks;
    for (size_t i = 0; i < tasks.size(); ++i) {
      tasks[i] = [](atomic_awaitable<int>& AA) -> tmc::task<void> {
        ++AA.ref();
        AA.ref().notify_all();
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

#undef CATEGORY

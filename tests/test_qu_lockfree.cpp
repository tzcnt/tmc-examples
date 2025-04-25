// Various tests to increase code coverage of qu_lockfree in ways that it is
// not normally used by the library.

#include "test_common.hpp"
#include "tmc/detail/qu_lockfree.hpp"
#include "tmc/sync.hpp"

#include <gtest/gtest.h>

#include <atomic>

#define CATEGORY test_qu_lockfree

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, expand_implicit_producer_index) {
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

TEST_F(CATEGORY, expand_explicit_producer_index) {
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

TEST_F(CATEGORY, destroy_implicit_non_empty) {
  std::atomic<size_t> count;
  {
    tmc::queue::ConcurrentQueue<destructor_counter> q;
    q.enqueue(destructor_counter(&count));
  }
  EXPECT_EQ(count, 1);
}

TEST_F(CATEGORY, destroy_explicit_non_empty) {
  using queue_t = tmc::queue::ConcurrentQueue<destructor_counter>;
  std::atomic<size_t> count;
  {
    // mimic the way that explicit producers are used by ex_cpu
    queue_t q;
    q.staticProducers = new queue_t::ExplicitProducer[1];
    q.staticProducers[0].init(&q);
    q.staticProducers[0].enqueue(destructor_counter(&count));
    delete[] q.staticProducers;
  }
  EXPECT_EQ(count, 1);
}

#undef CATEGORY

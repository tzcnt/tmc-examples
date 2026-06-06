// Various tests to increase code coverage of qu_mc in ways that it is
// not normally used by the library.

#include "test_common.hpp"
#include "tmc/detail/qu_mc.hpp"
#include "tmc/sync.hpp"

#include <gtest/gtest.h>

#include <atomic>

#define CATEGORY test_qu_mc

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
    ex(), tmc::iter_adapter(0, [](int) -> tmc::task<void> { co_return; }), 8000
  );

  auto t2 = tmc::post_bulk_waitable(
    ex(), tmc::iter_adapter(0, [](int) -> tmc::task<void> { co_return; }), 32000
  );
  t1.wait();
  t2.wait();
}

TEST_F(CATEGORY, destroy_implicit_non_empty) {
  std::atomic<size_t> destroyCount;
  {
    tmc::queue::ConcurrentQueue<destructor_counter> q;
    q.enqueue(destructor_counter(&destroyCount));
  }
  EXPECT_EQ(destroyCount, 1);
}

TEST_F(CATEGORY, destroy_implicit_non_empty_multi_block) {
  using queue_t = tmc::queue::ConcurrentQueue<destructor_counter>;
  auto Count = queue_t::ConcurrentQueue::PRODUCER_BLOCK_SIZE;
  std::atomic<size_t> destroyCount;
  {
    tmc::queue::ConcurrentQueue<destructor_counter> q;
    for (size_t i = 0; i < Count + 1; ++i) {
      q.enqueue(destructor_counter(&destroyCount));
    }
  }
  EXPECT_EQ(destroyCount, Count + 1);
}

#undef CATEGORY

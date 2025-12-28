#include "test_common.hpp"
#include "tmc/current.hpp"

#include <gtest/gtest.h>

#include <atomic>

#define CATEGORY test_ex_cpu_st

class CATEGORY : public testing::Test {
  static inline tmc::ex_cpu_st exec;

protected:
  static void SetUpTestSuite() { exec.set_priority_count(2).init(); }

  static void TearDownTestSuite() { exec.teardown(); }

  static tmc::ex_cpu_st& ex() { return exec; }
};

// If an invalid priority is submitted, ex_cpu_st will clamp it to the valid
// range.
TEST_F(CATEGORY, clamp_priority) {
  tmc::post_waitable(
    ex(),
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), ex().priority_count() - 1);
      co_return;
    }(),
    2
  )
    .wait();
}

TEST_F(CATEGORY, set_prio) {
  tmc::ex_cpu_st ex;
  ex.set_priority_count(1).init();
}

TEST_F(CATEGORY, set_thread_init_hook) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  {
    tmc::ex_cpu_st ex;
    ex.set_thread_init_hook([&](size_t tid) -> void { thr = tid; }).init();
  }
  EXPECT_EQ(thr.load(), 0);
}

TEST_F(CATEGORY, set_thread_teardown_hook) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  {
    tmc::ex_cpu_st ex;
    ex.set_thread_teardown_hook([&](size_t tid) -> void { thr = tid; }).init();
  }
  EXPECT_EQ(thr.load(), 0);
}

TEST_F(CATEGORY, thread_count) {
  tmc::ex_cpu_st ex;
  ex.init();
  EXPECT_EQ(ex.thread_count(), 1);
}

TEST_F(CATEGORY, no_init) { tmc::ex_cpu_st ex; }

TEST_F(CATEGORY, init_twice) {
  tmc::ex_cpu_st ex;
  ex.init();
  ex.init();
}

TEST_F(CATEGORY, teardown_twice) {
  tmc::ex_cpu_st ex;
  ex.teardown();
  ex.teardown();
}

TEST_F(CATEGORY, teardown_and_destroy) {
  tmc::ex_cpu_st ex;
  ex.teardown();
}

TEST_F(CATEGORY, set_spins) {
  tmc::ex_cpu_st ex;
  ex.set_spins(8).init();
  std::atomic<bool> ran = false;
  tmc::post_waitable(ex, [&]() { ran = true; }, 0).wait();
  EXPECT_EQ(ran.load(), true);
}

TEST_F(CATEGORY, set_spins_zero) {
  tmc::ex_cpu_st ex;
  ex.set_spins(0).init();
  std::atomic<bool> ran = false;
  tmc::post_waitable(ex, [&]() { ran = true; }, 0).wait();
  EXPECT_EQ(ran.load(), true);
}

#ifdef TMC_USE_HWLOC
TEST_F(CATEGORY, add_partition_all) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::ALL);

  tmc::ex_cpu_st ex;
  ex.add_partition(filter).init();
}

TEST_F(CATEGORY, add_partition_cpu_kind) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);

  tmc::ex_cpu_st ex;
  ex.add_partition(filter).init();
}

TEST_F(CATEGORY, add_partition_core) {
  tmc::topology::topology_filter filter;
  filter.set_core_indexes({0});

  tmc::ex_cpu_st ex;
  ex.add_partition(filter).init();
}

TEST_F(CATEGORY, add_partition_group) {
  tmc::topology::topology_filter filter;
  filter.set_group_indexes({0});

  tmc::ex_cpu_st ex;
  ex.add_partition(filter).init();
}

TEST_F(CATEGORY, add_partition_numa) {
  tmc::topology::topology_filter filter;
  filter.set_numa_indexes({0});

  tmc::ex_cpu_st ex;
  ex.add_partition(filter).init();
}
#endif

#include "test_executors.ipp"
#include "test_nested_executors.ipp"
#include "test_spawn_composition.ipp"
#include "test_spawn_func_many.ipp"
#include "test_spawn_func_many_detach.ipp"
#include "test_spawn_func_many_each.ipp"
#include "test_spawn_func_many_fork.ipp"
#include "test_spawn_many.ipp"
#include "test_spawn_many_detach.ipp"
#include "test_spawn_many_each.ipp"
#include "test_spawn_many_fork.ipp"
#include "test_spawn_tuple.ipp"

#undef CATEGORY

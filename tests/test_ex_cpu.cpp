#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/topology.hpp"

#include <gtest/gtest.h>

#include <atomic>

#define CATEGORY test_ex_cpu

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

// If an invalid priority is submitted, ex_cpu will clamp it to the valid range.
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
  tmc::ex_cpu ex;
  ex.set_priority_count(1).init();
}

TEST_F(CATEGORY, set_thread_count) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  EXPECT_EQ(ex.thread_count(), 1);
}

TEST_F(CATEGORY, set_thread_init_hook) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  {
    tmc::ex_cpu ex;
    ex.set_thread_init_hook([&](size_t tid) -> void { thr = tid; })
      .set_thread_count(1)
      .init();
  }
  EXPECT_EQ(thr.load(), 0);
}

TEST_F(CATEGORY, set_thread_teardown_hook) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  {
    tmc::ex_cpu ex;
    ex.set_thread_teardown_hook([&](size_t tid) -> void { thr = tid; })
      .set_thread_count(1)
      .init();
  }
  EXPECT_EQ(thr.load(), 0);
}

#ifdef TMC_USE_HWLOC
TEST_F(CATEGORY, set_thread_occupancy) {
  tmc::ex_cpu ex;
  ex.set_thread_occupancy(1.0f).init();
}
#endif

TEST_F(CATEGORY, no_init) { tmc::ex_cpu ex; }

TEST_F(CATEGORY, init_twice) {
  tmc::ex_cpu ex;
  ex.init();
  ex.init();
}

TEST_F(CATEGORY, teardown_twice) {
  tmc::ex_cpu ex;
  ex.teardown();
  ex.teardown();
}

TEST_F(CATEGORY, teardown_and_destroy) {
  tmc::ex_cpu ex;
  ex.teardown();
}

TEST_F(CATEGORY, set_spins) {
  tmc::ex_cpu ex;
  ex.set_spins(8).set_thread_count(1).init();
  std::atomic<bool> ran = false;
  tmc::post_waitable(ex, [&]() { ran = true; }, 0).wait();
  EXPECT_EQ(ran.load(), true);
}

TEST_F(CATEGORY, set_spins_zero) {
  tmc::ex_cpu ex;
  ex.set_spins(0).set_thread_count(1).init();
  std::atomic<bool> ran = false;
  tmc::post_waitable(ex, [&]() { ran = true; }, 0).wait();
  EXPECT_EQ(ran.load(), true);
}

TEST_F(CATEGORY, set_work_stealing_strategy_lattice) {
  tmc::ex_cpu ex;
  ex.set_work_stealing_strategy(tmc::work_stealing_strategy::LATTICE_MATRIX)
    .set_thread_count(2)
    .init();
}

TEST_F(CATEGORY, set_work_stealing_strategy_hierarchy) {
  tmc::ex_cpu ex;
  ex.set_work_stealing_strategy(tmc::work_stealing_strategy::HIERARCHY_MATRIX)
    .set_thread_count(2)
    .init();
}

#ifdef TMC_USE_HWLOC
TEST_F(CATEGORY, fill_thread_occupancy) {
  tmc::ex_cpu ex;
  ex.fill_thread_occupancy().init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, set_thread_occupancy_with_cpu_kind) {
  tmc::ex_cpu ex;
  ex.set_thread_occupancy(1.0f, tmc::topology::cpu_kind::PERFORMANCE).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, set_thread_pinning_level_core) {
  tmc::ex_cpu ex;
  ex.set_thread_pinning_level(tmc::topology::thread_pinning_level::CORE).init();
}

TEST_F(CATEGORY, set_thread_pinning_level_group) {
  tmc::ex_cpu ex;
  ex.set_thread_pinning_level(tmc::topology::thread_pinning_level::GROUP)
    .init();
}

TEST_F(CATEGORY, set_thread_pinning_level_numa) {
  tmc::ex_cpu ex;
  ex.set_thread_pinning_level(tmc::topology::thread_pinning_level::NUMA).init();
}

TEST_F(CATEGORY, set_thread_pinning_level_none) {
  tmc::ex_cpu ex;
  ex.set_thread_pinning_level(tmc::topology::thread_pinning_level::NONE).init();
}

TEST_F(CATEGORY, set_thread_packing_strategy_pack) {
  tmc::ex_cpu ex;
  ex.set_thread_packing_strategy(tmc::topology::thread_packing_strategy::PACK)
    .set_thread_count(1)
    .init();
}

TEST_F(CATEGORY, set_thread_packing_strategy_fan) {
  tmc::ex_cpu ex;
  ex.set_thread_packing_strategy(tmc::topology::thread_packing_strategy::FAN)
    .set_thread_count(1)
    .init();
}

TEST_F(CATEGORY, set_thread_init_hook_thread_info) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  std::atomic<size_t> group_idx = TMC_ALL_ONES;
  {
    tmc::ex_cpu ex;
    ex.set_thread_init_hook([&](tmc::topology::thread_info info) -> void {
        thr = info.index;
        group_idx = info.group.index;
      })
      .set_thread_count(1)
      .init();
  }
  EXPECT_EQ(thr.load(), 0);
  EXPECT_EQ(group_idx.load(), 0);
}

TEST_F(CATEGORY, set_thread_teardown_hook_thread_info) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  std::atomic<size_t> group_idx = TMC_ALL_ONES;
  {
    tmc::ex_cpu ex;
    ex.set_thread_teardown_hook([&](tmc::topology::thread_info info) -> void {
        thr = info.index;
        group_idx = info.group.index;
      })
      .set_thread_count(1)
      .init();
  }
  EXPECT_EQ(thr.load(), 0);
  EXPECT_EQ(group_idx.load(), 0);
}

TEST_F(CATEGORY, topology_query) {
  auto topo = tmc::topology::query();
  EXPECT_GE(topo.core_count(), 1);
  EXPECT_GE(topo.pu_count(), topo.core_count());
  EXPECT_GE(topo.group_count(), 1);
  EXPECT_GE(topo.numa_count(), 1);
  EXPECT_GE(topo.groups.size(), 1);
  EXPECT_GE(topo.cpu_kind_counts.size(), 1);
}

TEST_F(CATEGORY, topology_filter_core_indexes) {
  tmc::topology::topology_filter filter;
  filter.set_core_indexes({0});
  EXPECT_EQ(filter.core_indexes().size(), 1);
  EXPECT_EQ(filter.core_indexes()[0], 0);
}

TEST_F(CATEGORY, topology_filter_group_indexes) {
  tmc::topology::topology_filter filter;
  filter.set_group_indexes({0});
  EXPECT_EQ(filter.group_indexes().size(), 1);
  EXPECT_EQ(filter.group_indexes()[0], 0);
}

TEST_F(CATEGORY, topology_filter_numa_indexes) {
  tmc::topology::topology_filter filter;
  filter.set_numa_indexes({0});
  EXPECT_EQ(filter.numa_indexes().size(), 1);
  EXPECT_EQ(filter.numa_indexes()[0], 0);
}

TEST_F(CATEGORY, topology_filter_cpu_kinds) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::ALL);
  EXPECT_EQ(filter.cpu_kinds(), tmc::topology::cpu_kind::ALL);
}

TEST_F(CATEGORY, topology_filter_or) {
  tmc::topology::topology_filter filter1;
  filter1.set_core_indexes({0});
  tmc::topology::topology_filter filter2;
  filter2.set_core_indexes({1});
  auto combined = filter1 | filter2;
  EXPECT_EQ(combined.core_indexes().size(), 2);
}

TEST_F(CATEGORY, topology_is_hybrid) {
  auto topo = tmc::topology::query();
  // Result depends on hardware
  topo.is_hybrid();
}

TEST_F(CATEGORY, set_thread_occupancy_efficiency1) {
  tmc::ex_cpu ex;
  ex.set_thread_occupancy(0.5f, tmc::topology::cpu_kind::EFFICIENCY1).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, set_thread_occupancy_all) {
  tmc::ex_cpu ex;
  ex.set_thread_occupancy(1.0f, tmc::topology::cpu_kind::ALL).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, set_thread_occupancy_low) {
  tmc::topology::topology_filter f;
  f.set_group_indexes({0});

  tmc::ex_cpu ex;
  ex.add_partition(f)
    .set_thread_occupancy(0.01f, tmc::topology::cpu_kind::ALL)
    .init();
  EXPECT_EQ(ex.thread_count(), 1);
}

TEST_F(CATEGORY, add_partition_multiple_disjoint) {
  auto topo = tmc::topology::query();
  if (topo.core_count() < 2) {
    GTEST_SKIP() << "System has single core. Skipping multiple partition test.";
  }
  tmc::topology::topology_filter filter1;
  filter1.set_core_indexes({0});
  tmc::topology::topology_filter filter2;
  filter2.set_core_indexes({1});

  tmc::ex_cpu ex;
  ex.set_priority_count(2)
    .add_partition(filter1, 0, 1)
    .add_partition(filter2, 1, 2)
    .set_thread_pinning_level(tmc::topology::thread_pinning_level::CORE)
    .init();
  EXPECT_EQ(ex.thread_count(), 2);

  // Test cross-posting between the different priority groups
  // Sleep first so all of the executor threads go to sleep
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  tmc::post_waitable(
    ex,
    []() -> tmc::task<void> {
      co_await tmc::spawn([]() -> tmc::task<void> { co_return; }())
        .with_priority(0);
    }(),
    1
  )
    .wait();
}

TEST_F(CATEGORY, add_partition_multiple_overlap) {
  auto topo = tmc::topology::query();
  if (topo.core_count() < 2) {
    GTEST_SKIP() << "System has single core. Skipping multiple partition test.";
  }
  tmc::topology::topology_filter filter1;
  filter1.set_core_indexes({0});
  tmc::topology::topology_filter filter2;
  filter2.set_core_indexes({1});

  tmc::ex_cpu ex;
  ex.set_priority_count(3)
    .add_partition(filter1, 0, 2)
    .add_partition(filter2, 1, 3)
    .set_thread_pinning_level(tmc::topology::thread_pinning_level::CORE)
    .init();
  EXPECT_EQ(ex.thread_count(), 2);
}

TEST_F(CATEGORY, add_partition_hybrid_disjoint) {
  auto topo = tmc::topology::query();
  if (!topo.is_hybrid()) {
    GTEST_SKIP() << "System is not hybrid. Skipping hybrid partition test.";
  }
  tmc::topology::topology_filter filter1;
  filter1.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
  tmc::topology::topology_filter filter2;
  filter2.set_cpu_kinds(tmc::topology::cpu_kind::EFFICIENCY1);

  tmc::ex_cpu ex;
  ex.set_priority_count(2)
    .add_partition(filter1, 0, 1)
    .add_partition(filter2, 1, 2)
    .init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, add_partition_hybrid_overlap) {
  auto topo = tmc::topology::query();
  if (!topo.is_hybrid()) {
    GTEST_SKIP() << "System is not hybrid. Skipping hybrid partition test.";
  }
  tmc::topology::topology_filter filter1;
  filter1.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
  tmc::topology::topology_filter filter2;
  filter2.set_cpu_kinds(tmc::topology::cpu_kind::EFFICIENCY1);

  tmc::ex_cpu ex;
  ex.set_priority_count(3)
    .add_partition(filter1, 0, 2)
    .add_partition(filter2, 1, 3)
    .init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, post_with_thread_hint) {
  tmc::post_waitable(
    ex(), []() -> tmc::task<void> { co_return; }(), 0, 0
  )
    .wait();
}

TEST_F(CATEGORY, add_partition_all) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::ALL);

  tmc::ex_cpu ex;
  ex.add_partition(filter).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, add_partition_cpu_kind) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);

  tmc::ex_cpu ex;
  ex.add_partition(filter).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, add_partition_core) {
  tmc::topology::topology_filter filter;
  filter.set_core_indexes({0});

  tmc::ex_cpu ex;
  ex.add_partition(filter).init();
  EXPECT_EQ(ex.thread_count(), 1);
}

TEST_F(CATEGORY, add_partition_group) {
  tmc::topology::topology_filter filter;
  filter.set_group_indexes({0});

  tmc::ex_cpu ex;
  ex.add_partition(filter).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, add_partition_numa) {
  tmc::topology::topology_filter filter;
  filter.set_numa_indexes({0});

  tmc::ex_cpu ex;
  ex.add_partition(filter).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, add_partition_priority_range_end_clamp) {
  tmc::ex_cpu ex;
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
  ex.set_priority_count(2).add_partition(filter, 0, 5).init();
  EXPECT_GE(ex.thread_count(), 1);
}

TEST_F(CATEGORY, partition_split_group_thread_hint) {
  auto topo = tmc::topology::query();

  // This test requires a group with at least 2 cores
  if (topo.groups[0].core_indexes.size() < 2) {
    GTEST_SKIP();
  }

  tmc::topology::topology_filter f1;
  f1.set_core_indexes({0});
  tmc::topology::topology_filter f2;
  f2.set_core_indexes({1});

  // Split the 2 threads (which share a group) into separate priorities
  tmc::ex_cpu ex;
  ex.add_partition(f1, 0, 1)
    .add_partition(f2, 1, 2)
    .set_thread_pinning_level(tmc::topology::thread_pinning_level::CORE)
    .set_priority_count(2)
    .init();
  EXPECT_EQ(ex.thread_count(), 2);

  // Regardless of what ThreadHint we pass, it should end up on the correct
  // thread for the priority
  tmc::post_waitable(
    ex,
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_thread_index(), 0);
      co_return;
    }(),
    0, 0
  )
    .wait();
  tmc::post_waitable(
    ex,
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_thread_index(), 1);
      co_return;
    }(),
    1, 0
  )
    .wait();
  tmc::post_waitable(
    ex,
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_thread_index(), 0);
      co_return;
    }(),
    0, 1
  )
    .wait();
  tmc::post_waitable(
    ex,
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_thread_index(), 1);
      co_return;
    }(),
    1, 1
  )
    .wait();
}

TEST_F(CATEGORY, partition_split_group_thread_hint_overlap) {
  auto topo = tmc::topology::query();

  // This test requires a group with at least 2 cores
  if (topo.groups[0].core_indexes.size() < 2) {
    GTEST_SKIP();
  }

  // Same as previous test, but the 2nd filter is the entire group, which
  // overlaps the first filter
  tmc::topology::topology_filter f1;
  f1.set_core_indexes({0});
  tmc::topology::topology_filter f2;
  f2.set_group_indexes({0});

  // Split the 2 threads (which share a group) into separate priorities
  tmc::ex_cpu ex;
  ex.add_partition(f1, 0, 1)
    .add_partition(f2, 1, 2)
    .set_thread_pinning_level(tmc::topology::thread_pinning_level::CORE)
    .set_priority_count(2)
    .set_thread_count(2)
    .init();
  EXPECT_EQ(ex.thread_count(), 2);

  // Priority 0 tasks must run on thread 0.
  // Priority 1 tasks could run on either thread.
  tmc::post_waitable(
    ex,
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_thread_index(), 0);
      co_return;
    }(),
    0, 0
  )
    .wait();
  tmc::post_waitable(
    ex,
    []() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_thread_index(), 0);
      co_return;
    }(),
    0, 1
  )
    .wait();
  tmc::post_waitable(ex, []() -> tmc::task<void> { co_return; }(), 1, 0).wait();
  tmc::post_waitable(ex, []() -> tmc::task<void> { co_return; }(), 1, 1).wait();
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

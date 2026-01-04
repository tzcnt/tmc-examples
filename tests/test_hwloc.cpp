// Tests for behaviors that depend on hwloc.
// These tests don't depend on any specific hardware configuration, but have
// baseline expectations that should work on any machine.
#include "test_common.hpp"

#include <gtest/gtest.h>

#ifdef TMC_USE_HWLOC
#include "tmc/detail/thread_layout.hpp"
#include "tmc/topology.hpp"

#include <hwloc.h>
#include <set>

#define CATEGORY test_hwloc

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, query_returns_valid_topology) {
  auto topo = tmc::topology::query();
  EXPECT_GT(topo.groups.size(), 0u);
  EXPECT_GT(topo.cpu_kind_counts.size(), 0u);
  EXPECT_GT(topo.cpu_kind_counts[0], 0u);
  EXPECT_GT(topo.core_count(), 0u);
  EXPECT_GT(topo.pu_count(), 0u);
  EXPECT_GE(topo.pu_count(), topo.core_count());
  EXPECT_GT(topo.group_count(), 0u);
  EXPECT_GT(topo.numa_count(), 0u);
}

TEST_F(CATEGORY, query_groups_have_valid_structure) {
  auto topo = tmc::topology::query();
  for (auto& group : topo.groups) {
    EXPECT_GT(group.core_indexes.size(), 0u);
    EXPECT_GT(group.smt_level, 0u);
    EXPECT_NE(group.cpu_kind, 0u);
  }
}

TEST_F(CATEGORY, query_groups_core_indexes_are_contiguous) {
  auto topo = tmc::topology::query();
  std::set<size_t> all_cores;
  for (auto& group : topo.groups) {
    for (size_t idx : group.core_indexes) {
      EXPECT_TRUE(all_cores.find(idx) == all_cores.end())
        << "Core index " << idx << " appears in multiple groups";
      all_cores.insert(idx);
    }
  }
  EXPECT_EQ(all_cores.size(), topo.core_count());
  size_t expected = 0;
  for (size_t idx : all_cores) {
    EXPECT_EQ(idx, expected) << "Core indexes are not contiguous";
    ++expected;
  }
}

TEST_F(CATEGORY, query_groups_sorted_by_index) {
  auto topo = tmc::topology::query();
  for (size_t i = 0; i < topo.groups.size(); ++i) {
    EXPECT_EQ(topo.groups[i].index, i);
  }
}

TEST_F(CATEGORY, query_cpu_kind_counts_sum_to_core_count) {
  auto topo = tmc::topology::query();
  size_t sum = 0;
  for (size_t count : topo.cpu_kind_counts) {
    sum += count;
  }
  EXPECT_EQ(sum, topo.core_count());
}

TEST_F(CATEGORY, query_is_hybrid_matches_cpu_kind_counts) {
  auto topo = tmc::topology::query();
  EXPECT_EQ(topo.is_hybrid(), topo.cpu_kind_counts.size() > 1);
}

TEST_F(CATEGORY, query_internal_returns_valid_topology) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  EXPECT_NE(hwlocTopo, nullptr);
  EXPECT_GT(topo.cores.size(), 0u);
  EXPECT_GT(topo.groups.size(), 0u);
  EXPECT_GT(topo.cpu_kind_counts.size(), 0u);
}

TEST_F(CATEGORY, query_internal_cores_have_valid_pus) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  for (auto& core : topo.cores) {
    EXPECT_GT(core.pus.size(), 0u);
    for (auto pu : core.pus) {
      EXPECT_NE(pu, nullptr);
    }
    EXPECT_NE(core.cpuset, nullptr);
    EXPECT_NE(core.cache, nullptr);
  }
}

TEST_F(CATEGORY, query_internal_cores_have_sequential_indexes) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  for (size_t i = 0; i < topo.cores.size(); ++i) {
    EXPECT_EQ(topo.cores[i].index, i);
  }
}

TEST_F(CATEGORY, query_internal_is_idempotent) {
  hwloc_topology_t hwlocTopo1;
  auto topo1 = tmc::topology::detail::query_internal(hwlocTopo1);
  hwloc_topology_t hwlocTopo2;
  auto topo2 = tmc::topology::detail::query_internal(hwlocTopo2);
  EXPECT_EQ(hwlocTopo1, hwlocTopo2);
  EXPECT_EQ(topo1.cores.size(), topo2.cores.size());
  EXPECT_EQ(topo1.groups.size(), topo2.groups.size());
  EXPECT_EQ(topo1.cpu_kind_counts, topo2.cpu_kind_counts);
}

TEST_F(CATEGORY, flatten_groups_on_real_topology) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flat = tmc::topology::detail::flatten_groups(topo.groups);
  EXPECT_GT(flat.size(), 0u);
  for (auto* group : flat) {
    EXPECT_GE(group->index, 0);
    EXPECT_TRUE(group->children.empty());
  }
}

TEST_F(CATEGORY, adjust_thread_groups_reduces_thread_count) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flat = tmc::topology::detail::flatten_groups(topo.groups);

  size_t originalTotal = 0;
  for (auto* g : flat) {
    originalTotal += g->group_size;
  }
  EXPECT_EQ(originalTotal, topo.cores.size());
  if (originalTotal <= 1) {
    GTEST_SKIP() << "Need more than 1 core to test thread reduction";
  }

  size_t requestedCount = originalTotal / 2;
  if (requestedCount == 0) {
    requestedCount = 1;
  }

  tmc::detail::adjust_thread_groups(
    requestedCount, {}, flat, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );

  size_t newTotal = 0;
  for (auto* g : flat) {
    newTotal += g->group_size;
  }
  EXPECT_EQ(newTotal, requestedCount);
}

TEST_F(CATEGORY, adjust_thread_groups_fan_strategy) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flat = tmc::topology::detail::flatten_groups(topo.groups);

  size_t originalTotal = 0;
  for (auto* g : flat) {
    originalTotal += g->group_size;
  }
  if (originalTotal <= 1) {
    GTEST_SKIP() << "Need more than 1 core to test FAN strategy";
  }

  size_t requestedCount = originalTotal / 2;
  if (requestedCount == 0) {
    requestedCount = 1;
  }

  tmc::detail::adjust_thread_groups(
    requestedCount, {}, flat, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::FAN
  );

  size_t newTotal = 0;
  for (auto* g : flat) {
    newTotal += g->group_size;
  }
  EXPECT_EQ(newTotal, requestedCount);
}

TEST_F(CATEGORY, adjust_thread_groups_with_cpu_kind_filter) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flat = tmc::topology::detail::flatten_groups(topo.groups);

  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);

  size_t originalTotal = 0;
  for (auto* g : flat) {
    originalTotal += g->group_size;
  }

  tmc::detail::adjust_thread_groups(
    0, {}, flat, filter, tmc::topology::thread_packing_strategy::PACK
  );

  size_t newTotal = 0;
  for (auto* g : flat) {
    newTotal += g->group_size;
  }
  EXPECT_GT(newTotal, 0u);
  EXPECT_LE(newTotal, originalTotal);
}

TEST_F(CATEGORY, find_parent_cache_returns_valid_cache) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  ASSERT_GT(topo.cores.size(), 0u);

  auto& firstCore = topo.cores[0];
  ASSERT_NE(firstCore.cache, nullptr);

  auto parentCache = tmc::topology::detail::find_parent_cache(firstCore.cache);
  EXPECT_NE(parentCache, nullptr);
  EXPECT_GE(parentCache->type, HWLOC_OBJ_L1CACHE);
  EXPECT_LE(parentCache->type, HWLOC_OBJ_L5CACHE);
}

TEST_F(CATEGORY, public_group_from_private_on_real_data) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flat = tmc::topology::detail::flatten_groups(topo.groups);
  ASSERT_GT(flat.size(), 0u);

  for (auto* privateGroup : flat) {
    auto publicGroup = tmc::detail::public_group_from_private(*privateGroup);
    EXPECT_EQ(publicGroup.index, static_cast<size_t>(privateGroup->index));
    EXPECT_EQ(publicGroup.core_indexes.size(), privateGroup->cores.size());
    EXPECT_GT(publicGroup.smt_level, 0u);
    EXPECT_NE(publicGroup.cpu_kind, 0u);
  }
}

TEST_F(CATEGORY, make_partition_cpuset_default_filter) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  tmc::topology::topology_filter filter;
  tmc::topology::cpu_kind::value cpuKind;

  auto cpuset =
    tmc::detail::make_partition_cpuset(hwlocTopo, topo, filter, cpuKind);

  EXPECT_NE(static_cast<hwloc_bitmap_t>(cpuset), nullptr);
  EXPECT_GT(hwloc_bitmap_weight(cpuset), 0);
  EXPECT_NE(cpuKind, 0u);
}

TEST_F(CATEGORY, make_partition_cpuset_performance_only) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
  tmc::topology::cpu_kind::value cpuKind;

  auto cpuset =
    tmc::detail::make_partition_cpuset(hwlocTopo, topo, filter, cpuKind);

  EXPECT_NE(static_cast<hwloc_bitmap_t>(cpuset), nullptr);
  EXPECT_GT(hwloc_bitmap_weight(cpuset), 0);
}

TEST_F(CATEGORY, pin_thread) {
  tmc::topology::topology_filter filter;
  EXPECT_NO_THROW(tmc::topology::pin_thread(filter));
}

TEST_F(CATEGORY, pin_thread_with_performance_filter) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
  EXPECT_NO_THROW(tmc::topology::pin_thread(filter));
}

TEST_F(CATEGORY, query_matches_query_internal) {
  auto publicTopo = tmc::topology::query();
  hwloc_topology_t hwlocTopo;
  auto privateTopo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flatGroups = tmc::topology::detail::flatten_groups(privateTopo.groups);

  EXPECT_EQ(publicTopo.groups.size(), flatGroups.size());
  EXPECT_EQ(publicTopo.cpu_kind_counts, privateTopo.cpu_kind_counts);
  EXPECT_EQ(publicTopo.is_hybrid(), privateTopo.is_hybrid());
}

TEST_F(CATEGORY, topology_filter_filters_cores) {
  auto topo = tmc::topology::query();
  if (topo.core_count() < 2) {
    GTEST_SKIP() << "Need at least 2 cores to test core filtering";
  }

  hwloc_topology_t hwlocTopo;
  auto privateTopo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flat = tmc::topology::detail::flatten_groups(privateTopo.groups);

  size_t originalTotal = 0;
  for (auto* g : flat) {
    originalTotal += g->group_size;
  }

  tmc::topology::topology_filter filter;
  filter.set_core_indexes({0});

  tmc::detail::adjust_thread_groups(
    0, {}, flat, filter, tmc::topology::thread_packing_strategy::PACK
  );

  size_t newTotal = 0;
  for (auto* g : flat) {
    newTotal += g->group_size;
  }
  EXPECT_EQ(newTotal, 1u);
  EXPECT_LT(newTotal, originalTotal);
}

TEST_F(CATEGORY, topology_filter_filters_groups) {
  auto topo = tmc::topology::query();
  if (topo.group_count() < 2) {
    GTEST_SKIP() << "Need at least 2 groups to test group filtering";
  }

  hwloc_topology_t hwlocTopo;
  auto privateTopo = tmc::topology::detail::query_internal(hwlocTopo);
  auto flat = tmc::topology::detail::flatten_groups(privateTopo.groups);

  size_t originalTotal = 0;
  for (auto* g : flat) {
    originalTotal += g->group_size;
  }
  size_t firstGroupCores = flat[0]->cores.size();

  tmc::topology::topology_filter filter;
  filter.set_group_indexes({0});

  tmc::detail::adjust_thread_groups(
    0, {}, flat, filter, tmc::topology::thread_packing_strategy::PACK
  );

  size_t newTotal = 0;
  for (auto* g : flat) {
    newTotal += g->group_size;
  }
  EXPECT_EQ(newTotal, firstGroupCores);
  EXPECT_LT(newTotal, originalTotal);
}

TEST_F(CATEGORY, hierarchical_matrix_on_real_topology) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);

  auto matrix = tmc::detail::get_hierarchical_matrix(topo.groups);

  size_t threadCount = 0;
  tmc::detail::for_all_groups(
    topo.groups, [&threadCount](tmc::topology::detail::CacheGroup& g) {
      threadCount += g.group_size;
    }
  );

  EXPECT_EQ(matrix.size(), threadCount * threadCount);
  for (size_t t = 0; t < threadCount; ++t) {
    EXPECT_EQ(matrix[t * threadCount], t);
  }
}

TEST_F(CATEGORY, lattice_matrix_on_real_topology) {
  hwloc_topology_t hwlocTopo;
  auto topo = tmc::topology::detail::query_internal(hwlocTopo);

  auto matrix = tmc::detail::get_lattice_matrix(topo.groups);

  size_t threadCount = 0;
  tmc::detail::for_all_groups(
    topo.groups, [&threadCount](tmc::topology::detail::CacheGroup& g) {
      threadCount += g.group_size;
    }
  );

  EXPECT_EQ(matrix.size(), threadCount * threadCount);
  for (size_t t = 0; t < threadCount; ++t) {
    EXPECT_EQ(matrix[t * threadCount], t);
  }
}

TEST_F(CATEGORY, thread_init_hook_receives_valid_info) {
  tmc::ex_cpu testExec;
  std::atomic<size_t> hookCallCount{0};
  std::vector<tmc::topology::thread_info> infos;
  std::mutex infosMutex;

  testExec.set_thread_count(2)
    .set_thread_init_hook([&](tmc::topology::thread_info info) {
      std::lock_guard<std::mutex> lock(infosMutex);
      infos.push_back(info);
      ++hookCallCount;
    })
    .init();

  while (hookCallCount.load() < 2) {
    std::this_thread::yield();
  }

  testExec.teardown();

  EXPECT_EQ(infos.size(), 2u);
  std::set<size_t> threadIndexes;
  for (auto& info : infos) {
    EXPECT_GT(info.group.core_indexes.size(), 0u);
    EXPECT_GT(info.group.smt_level, 0u);
    threadIndexes.insert(info.index);
  }
  EXPECT_EQ(threadIndexes.size(), 2u);
}

TEST_F(CATEGORY, thread_teardown_hook_receives_valid_info) {
  tmc::ex_cpu testExec;
  std::atomic<size_t> hookCallCount{0};
  std::vector<tmc::topology::thread_info> infos;
  std::mutex infosMutex;

  testExec.set_thread_count(2)
    .set_thread_teardown_hook([&](tmc::topology::thread_info info) {
      std::lock_guard<std::mutex> lock(infosMutex);
      infos.push_back(info);
      ++hookCallCount;
    })
    .init();

  testExec.teardown();

  while (hookCallCount.load() < 2) {
    std::this_thread::yield();
  }

  EXPECT_EQ(infos.size(), 2u);
  for (auto& info : infos) {
    EXPECT_GT(info.group.core_indexes.size(), 0u);
  }
}

#undef CATEGORY

#endif // TMC_USE_HWLOC

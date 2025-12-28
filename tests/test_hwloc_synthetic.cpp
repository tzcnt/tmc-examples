// Tests for behaviors that depend on hwloc.
// These tests don't depend on any specific hardware configuration, but have
// baseline expectations that should work on any machine.
#include "test_common.hpp"

#include <gtest/gtest.h>

#ifdef TMC_USE_HWLOC
#include "fixtures/fixtures.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/detail/thread_layout.hpp"
#include "tmc/topology.hpp"

#include <hwloc.h>

#define CATEGORY test_hwloc_synthetic

class CATEGORY : public testing::Test {
protected:
  static inline hwloc_topology_t hwlocTopo;
  static inline tmc::topology::detail::Topology tmcTopo;
  static void SetUpTestSuite() {
    // Use an exported XML from the Intel 13600k
    tmcTopo = tmc::topology::detail::query_internal(
      hwlocTopo, topo_13600k.c_str(), topo_13600k.size()
    );
  }

  static void TearDownTestSuite() { hwloc_topology_destroy(hwlocTopo); }
};

TEST_F(CATEGORY, load_synthetic_topology) {
  EXPECT_TRUE(tmcTopo.is_hybrid());
  EXPECT_EQ(tmcTopo.groups.size(), 2);
  EXPECT_EQ(tmcTopo.groups[0].index, 0);
  EXPECT_EQ(tmcTopo.groups[0].group_size, 6);
  EXPECT_EQ(tmcTopo.groups[0].children.size(), 0);

  EXPECT_EQ(tmcTopo.groups[1].index, TMC_ALL_ONES);
  EXPECT_EQ(tmcTopo.groups[1].group_size, 0);
  EXPECT_EQ(tmcTopo.groups[1].children.size(), 2);

  EXPECT_EQ(tmcTopo.groups[1].children[0].index, 1);
  EXPECT_EQ(tmcTopo.groups[1].children[0].group_size, 4);
  EXPECT_EQ(tmcTopo.groups[1].children[0].children.size(), 0);

  EXPECT_EQ(tmcTopo.groups[1].children[1].index, 2);
  EXPECT_EQ(tmcTopo.groups[1].children[1].group_size, 4);
  EXPECT_EQ(tmcTopo.groups[1].children[1].children.size(), 0);
}

TEST_F(CATEGORY, get_all_pu_indexes_first_group_empty) {
  auto groupsCopy = tmcTopo.groups;
  auto flatGroups = tmc::topology::detail::flatten_groups(groupsCopy);
  ASSERT_FALSE(flatGroups.empty());
  flatGroups[0]->group_size = 0;

  bool hasNonEmptyGroup = false;
  for (auto* g : flatGroups) {
    if (g->group_size != 0) {
      hasNonEmptyGroup = true;
      break;
    }
  }
  ASSERT_TRUE(hasNonEmptyGroup);

  auto puToThread = tmc::detail::get_all_pu_indexes(flatGroups);
  EXPECT_FALSE(puToThread.empty());

  size_t firstNonEmptyGroupStart = 0;
  for (auto* g : flatGroups) {
    if (g->group_size != 0) {
      firstNonEmptyGroupStart = g->group_start;
      break;
    }
  }
  for (size_t puIdx : puToThread) {
    EXPECT_GE(puIdx, firstNonEmptyGroupStart);
  }
}

TEST_F(CATEGORY, make_partition_cpuset_exclude_first_group) {
  tmc::topology::topology_filter filter;
  filter.set_group_indexes({1, 2});

  tmc::topology::cpu_kind::value cpuKindOut;
  auto cpuset =
    tmc::detail::make_partition_cpuset(hwlocTopo, tmcTopo, filter, cpuKindOut);

  EXPECT_NE(cpuset.obj, nullptr);
  int weight = hwloc_bitmap_weight(cpuset);
  EXPECT_GT(weight, 0);

  auto groupsCopy = tmcTopo.groups;
  auto flatGroups = tmc::topology::detail::flatten_groups(groupsCopy);
  ASSERT_GE(flatGroups.size(), 3u);

  for (auto& core : flatGroups[0]->cores) {
    for (auto* pu : core.pus) {
      EXPECT_FALSE(hwloc_bitmap_isset(cpuset, pu->os_index))
        << "PU " << pu->os_index << " from excluded group 0 should not be set";
    }
  }

  bool hasGroup1Or2PU = false;
  for (size_t gidx = 1; gidx < flatGroups.size() && gidx <= 2; ++gidx) {
    for (auto& core : flatGroups[gidx]->cores) {
      for (auto* pu : core.pus) {
        if (hwloc_bitmap_isset(cpuset, pu->os_index)) {
          hasGroup1Or2PU = true;
        }
      }
    }
  }
  EXPECT_TRUE(hasGroup1Or2PU);
}

TEST_F(CATEGORY, make_partition_cpuset_exclude_performance_cores) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::EFFICIENCY1);

  tmc::topology::cpu_kind::value cpuKindOut;
  auto cpuset =
    tmc::detail::make_partition_cpuset(hwlocTopo, tmcTopo, filter, cpuKindOut);

  hwloc_bitmap_t rawCpuset = cpuset;
  EXPECT_NE(rawCpuset, nullptr);
  int weight = hwloc_bitmap_weight(rawCpuset);
  EXPECT_GT(weight, 0);

  for (auto& core : tmcTopo.cores) {
    if (core.cpu_kind == 0) {
      for (auto* pu : core.pus) {
        EXPECT_FALSE(hwloc_bitmap_isset(rawCpuset, pu->os_index))
          << "PU " << pu->os_index
          << " from PERFORMANCE core should not be set";
      }
    }
  }

  bool hasEfficiencyPU = false;
  for (auto& core : tmcTopo.cores) {
    if (core.cpu_kind == 1) {
      for (auto* pu : core.pus) {
        if (hwloc_bitmap_isset(rawCpuset, pu->os_index)) {
          hasEfficiencyPU = true;
        }
      }
    }
  }
  EXPECT_TRUE(hasEfficiencyPU);

  EXPECT_EQ(cpuKindOut, tmc::topology::cpu_kind::EFFICIENCY1);
}

#undef CATEGORY

#endif // TMC_USE_HWLOC

// Various tests to increase code coverage in specific areas that are otherwise
// not exercised.
#include "test_common.hpp"
#include "tmc/detail/matrix.hpp"
#include "tmc/detail/thread_layout.hpp"
#include "tmc/topology.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_thread_layout

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, group_iteration_order) {
  tmc::detail::get_flat_group_iteration_order(16, 5);
}

static std::vector<tmc::topology::detail::CacheGroup> get_core_groups() {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 16; ++i) {
    groupedCores.push_back(
      tmc::topology::detail::CacheGroup{
        nullptr, static_cast<int>(i), 0, {}, {}, 4, i * 4
      }
    );
  }
  return groupedCores;
}

TEST_F(CATEGORY, get_matrixes) {
  auto groupedCores = get_core_groups();
  size_t threadCount = 0;
  for (size_t i = 0; i < groupedCores.size(); ++i) {
    threadCount += groupedCores[i].group_size;
  }
  {
    std::vector<size_t> steal_matrix =
      tmc::detail::get_lattice_matrix(groupedCores);
    EXPECT_EQ(steal_matrix.size(), threadCount * threadCount);
    auto waker_matrix = tmc::detail::invert_matrix(steal_matrix, threadCount);
    EXPECT_EQ(waker_matrix.size(), threadCount * threadCount);
  }
  {
    std::vector<size_t> steal_matrix =
      tmc::detail::get_hierarchical_matrix(groupedCores);
    EXPECT_EQ(steal_matrix.size(), threadCount * threadCount);
    auto waker_matrix = tmc::detail::invert_matrix(steal_matrix, threadCount);
    EXPECT_EQ(waker_matrix.size(), threadCount * threadCount);
  }
}

TEST_F(CATEGORY, group_iteration_order_empty) {
  auto order = tmc::detail::get_flat_group_iteration_order(0, 0);
  EXPECT_TRUE(order.empty());
}

TEST_F(CATEGORY, group_iteration_order_single) {
  auto order = tmc::detail::get_flat_group_iteration_order(1, 0);
  EXPECT_EQ(order.size(), 1u);
  EXPECT_EQ(order[0], 0u);
}

TEST_F(CATEGORY, group_iteration_order_starts_with_start_group) {
  for (size_t count = 2; count <= 16; ++count) {
    for (size_t start = 0; start < count; ++start) {
      auto order = tmc::detail::get_flat_group_iteration_order(count, start);
      EXPECT_EQ(order.size(), count);
      EXPECT_EQ(order[0], start);
    }
  }
}

TEST_F(CATEGORY, group_iteration_order_contains_all_groups) {
  for (size_t count = 1; count <= 16; ++count) {
    auto order = tmc::detail::get_flat_group_iteration_order(count, 0);
    std::vector<bool> seen(count, false);
    for (size_t idx : order) {
      EXPECT_LT(idx, count);
      seen[idx] = true;
    }
    for (size_t i = 0; i < count; ++i) {
      EXPECT_TRUE(seen[i]) << "Group " << i << " not in order";
    }
  }
}

TEST_F(CATEGORY, invert_matrix_identity) {
  std::vector<size_t> identity = {0, 1, 2, 3, 1, 2, 3, 0,
                                  2, 3, 0, 1, 3, 0, 1, 2};
  auto inverted = tmc::detail::invert_matrix(identity, 4);
  EXPECT_EQ(inverted.size(), 16u);
  auto double_inverted = tmc::detail::invert_matrix(inverted, 4);
  EXPECT_EQ(double_inverted, identity);
}

TEST_F(CATEGORY, invert_matrix_documented_example) {
  std::vector<size_t> forward = {0, 1, 2, 3, 4, 5, 6, 7, 1, 2, 3, 0, 5,
                                 6, 7, 4, 2, 3, 0, 1, 6, 7, 4, 5, 3, 0,
                                 1, 2, 7, 4, 5, 6, 4, 5, 6, 7, 0, 1, 2,
                                 3, 5, 6, 7, 4, 1, 2, 3, 0, 6, 7, 4, 5,
                                 2, 3, 0, 1, 7, 4, 5, 6, 3, 0, 1, 2};
  std::vector<size_t> expected_inverted = {
    0, 3, 2, 1, 4, 7, 6, 5, 1, 0, 3, 2, 5, 4, 7, 6, 2, 1, 0, 3, 6, 5,
    4, 7, 3, 2, 1, 0, 7, 6, 5, 4, 4, 7, 6, 5, 0, 3, 2, 1, 5, 4, 7, 6,
    1, 0, 3, 2, 6, 5, 4, 7, 2, 1, 0, 3, 7, 6, 5, 4, 3, 2, 1, 0
  };
  auto inverted = tmc::detail::invert_matrix(forward, 8);
  EXPECT_EQ(inverted, expected_inverted);
}

TEST_F(CATEGORY, slice_matrix) {
  std::vector<size_t> matrix = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  auto slice0 = tmc::detail::slice_matrix(matrix, 3, 0);
  EXPECT_EQ(slice0, (std::vector<size_t>{0, 1, 2}));
  auto slice1 = tmc::detail::slice_matrix(matrix, 3, 1);
  EXPECT_EQ(slice1, (std::vector<size_t>{3, 4, 5}));
  auto slice2 = tmc::detail::slice_matrix(matrix, 3, 2);
  EXPECT_EQ(slice2, (std::vector<size_t>{6, 7, 8}));
}

TEST_F(CATEGORY, lattice_matrix_single_group) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  groups.push_back({nullptr, 0, 0, {}, {}, 4, 0});
  auto matrix = tmc::detail::get_lattice_matrix(groups);
  EXPECT_EQ(matrix.size(), 16u);
  for (size_t row = 0; row < 4; ++row) {
    EXPECT_EQ(matrix[row * 4], row);
  }
}

TEST_F(CATEGORY, hierarchical_matrix_single_group) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  groups.push_back({nullptr, 0, 0, {}, {}, 4, 0});
  auto matrix = tmc::detail::get_hierarchical_matrix(groups);
  EXPECT_EQ(matrix.size(), 16u);
  for (size_t row = 0; row < 4; ++row) {
    EXPECT_EQ(matrix[row * 4], row);
  }
}

TEST_F(CATEGORY, matrix_threads_steal_from_self_first) {
  auto groupedCores = get_core_groups();
  size_t threadCount = 0;
  for (auto& g : groupedCores) {
    threadCount += g.group_size;
  }
  auto lattice = tmc::detail::get_lattice_matrix(groupedCores);
  auto hierarchical = tmc::detail::get_hierarchical_matrix(groupedCores);
  for (size_t t = 0; t < threadCount; ++t) {
    EXPECT_EQ(lattice[t * threadCount], t);
    EXPECT_EQ(hierarchical[t * threadCount], t);
  }
}

TEST_F(CATEGORY, for_all_groups_flat) {
  auto groups = get_core_groups();
  size_t count = 0;
  tmc::detail::for_all_groups(
    groups, [&count](tmc::topology::detail::CacheGroup&) { ++count; }
  );
  EXPECT_EQ(count, groups.size());
}

TEST_F(CATEGORY, for_all_groups_nested) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  tmc::topology::detail::CacheGroup parent{nullptr, -1, 0, {}, {}, 0, 0};
  parent.children.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  parent.children.push_back({nullptr, 1, 0, {}, {}, 2, 2});
  groups.push_back(parent);
  std::vector<int> visited_indexes;
  tmc::detail::for_all_groups(
    groups, [&visited_indexes](tmc::topology::detail::CacheGroup& g) {
      visited_indexes.push_back(g.index);
    }
  );
  EXPECT_EQ(visited_indexes.size(), 2u);
  EXPECT_EQ(visited_indexes[0], 0);
  EXPECT_EQ(visited_indexes[1], 1);
}

TEST_F(CATEGORY, thread_cache_group_iterator_flat) {
  auto groups = get_core_groups();
  size_t count = 0;
  tmc::detail::ThreadCacheGroupIterator iter(
    groups, [&count](tmc::topology::detail::CacheGroup&) { ++count; }
  );
  while (iter.next()) {
  }
  EXPECT_EQ(count, groups.size());
}

TEST_F(CATEGORY, matrix_default_constructor) {
  tmc::detail::Matrix m;
  EXPECT_EQ(m.data, nullptr);
  EXPECT_EQ(m.rows, 0u);
  EXPECT_EQ(m.cols, 0u);
}

TEST_F(CATEGORY, matrix_init_value) {
  tmc::detail::Matrix m;
  m.init(42, 3, 4);
  EXPECT_EQ(m.rows, 3u);
  EXPECT_EQ(m.cols, 4u);
  for (size_t i = 0; i < 12; ++i) {
    EXPECT_EQ(m.data[i], 42u);
  }
}

TEST_F(CATEGORY, matrix_copy_from) {
  size_t src[] = {1, 2, 3, 4, 5, 6};
  tmc::detail::Matrix m;
  m.copy_from(src, 2, 3);
  EXPECT_EQ(m.rows, 2u);
  EXPECT_EQ(m.cols, 3u);
  EXPECT_EQ(m.data[0], 1u);
  EXPECT_EQ(m.data[5], 6u);
}

TEST_F(CATEGORY, matrix_init_from_vector_square) {
  std::vector<size_t> data = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  tmc::detail::Matrix m;
  m.init(std::move(data), 3);
  EXPECT_EQ(m.rows, 3u);
  EXPECT_EQ(m.cols, 3u);
  EXPECT_EQ(m.data[4], 4u);
  EXPECT_TRUE(data.empty());
}

TEST_F(CATEGORY, matrix_init_from_vector_rect) {
  std::vector<size_t> data = {0, 1, 2, 3, 4, 5};
  tmc::detail::Matrix m;
  m.init(std::move(data), 2, 3);
  EXPECT_EQ(m.rows, 2u);
  EXPECT_EQ(m.cols, 3u);
  EXPECT_EQ(m.data[5], 5u);
  EXPECT_TRUE(data.empty());
}

TEST_F(CATEGORY, matrix_move_constructor) {
  tmc::detail::Matrix m1;
  m1.init(7, 2, 2);
  tmc::detail::Matrix m2(std::move(m1));
  EXPECT_EQ(m2.rows, 2u);
  EXPECT_EQ(m2.cols, 2u);
  EXPECT_EQ(m2.data[0], 7u);
  EXPECT_EQ(m1.data, nullptr);
  EXPECT_EQ(m1.rows, 0u);
}

TEST_F(CATEGORY, matrix_move_assignment) {
  tmc::detail::Matrix m1;
  m1.init(3, 2, 2);
  tmc::detail::Matrix m2;
  m2.init(9, 3, 3);
  m2 = std::move(m1);
  EXPECT_EQ(m2.rows, 2u);
  EXPECT_EQ(m2.cols, 2u);
  EXPECT_EQ(m2.data[0], 3u);
  EXPECT_EQ(m1.data, nullptr);
}

TEST_F(CATEGORY, matrix_set_weak_ref) {
  tmc::detail::Matrix m1;
  m1.init(5, 2, 2);
  tmc::detail::Matrix m2;
  m2.set_weak_ref(m1);
  EXPECT_EQ(m2.data, m1.data);
  EXPECT_EQ(m2.rows, m1.rows);
  EXPECT_EQ(m2.cols, m1.cols);
  EXPECT_TRUE(m2.weak_ptr);
}

TEST_F(CATEGORY, matrix_get_row) {
  tmc::detail::Matrix m;
  m.init(0, 3, 4);
  for (size_t i = 0; i < 12; ++i) {
    m.data[i] = i;
  }
  EXPECT_EQ(m.get_row(0)[0], 0u);
  EXPECT_EQ(m.get_row(1)[0], 4u);
  EXPECT_EQ(m.get_row(2)[0], 8u);
}

TEST_F(CATEGORY, matrix_get_slice) {
  tmc::detail::Matrix m;
  m.init(0, 3, 3);
  for (size_t i = 0; i < 9; ++i) {
    m.data[i] = i;
  }
  auto slice = m.get_slice(1);
  EXPECT_EQ(slice.size(), 3u);
  EXPECT_EQ(slice[0], 3u);
  EXPECT_EQ(slice[1], 4u);
  EXPECT_EQ(slice[2], 5u);
}

TEST_F(CATEGORY, matrix_copy_row) {
  tmc::detail::Matrix m1;
  m1.init(0, 2, 3);
  for (size_t i = 0; i < 6; ++i) {
    m1.data[i] = i;
  }
  tmc::detail::Matrix m2;
  m2.init(99, 2, 3);
  m2.copy_row(0, 1, m1);
  EXPECT_EQ(m2.data[0], 3u);
  EXPECT_EQ(m2.data[1], 4u);
  EXPECT_EQ(m2.data[2], 5u);
}

TEST_F(CATEGORY, matrix_copy_row_different_sizes) {
  tmc::detail::Matrix m1;
  m1.init(0, 2, 4);
  for (size_t i = 0; i < 8; ++i) {
    m1.data[i] = i;
  }
  tmc::detail::Matrix m2;
  m2.init(99, 2, 2);
  m2.copy_row(0, 1, m1);
  EXPECT_EQ(m2.data[0], 4u);
  EXPECT_EQ(m2.data[1], 5u);
}

TEST_F(CATEGORY, matrix_to_wakers) {
  tmc::detail::Matrix m;
  std::vector<size_t> data = {0, 1, 2, 3, 1, 2, 3, 0, 2, 3, 0, 1, 3, 0, 1, 2};
  m.init(std::move(data), 4);
  auto wakers = m.to_wakers();
  EXPECT_EQ(wakers.rows, 4u);
  EXPECT_EQ(wakers.cols, 4u);
}

TEST_F(CATEGORY, matrix_clear) {
  tmc::detail::Matrix m;
  m.init(1, 3, 3);
  m.clear();
  EXPECT_EQ(m.data, nullptr);
  EXPECT_EQ(m.rows, 0u);
  EXPECT_EQ(m.cols, 0u);
}

TEST_F(CATEGORY, matrix_weak_ptr_clear) {
  tmc::detail::Matrix m1;
  m1.init(5, 2, 2);
  tmc::detail::Matrix m2;
  m2.set_weak_ref(m1);
  m2.clear();
  EXPECT_EQ(m2.data, nullptr);
  EXPECT_NE(m1.data, nullptr);
}

#ifdef TMC_USE_HWLOC
TEST_F(CATEGORY, topology_filter_default_values) {
  tmc::topology::topology_filter filter;
  EXPECT_TRUE(filter.core_indexes().empty());
  EXPECT_TRUE(filter.group_indexes().empty());
  EXPECT_TRUE(filter.numa_indexes().empty());
  EXPECT_EQ(
    filter.cpu_kinds(),
    tmc::topology::cpu_kind::PERFORMANCE | tmc::topology::cpu_kind::EFFICIENCY1
  );
}

TEST_F(CATEGORY, topology_filter_set_core_indexes) {
  tmc::topology::topology_filter filter;
  filter.set_core_indexes({3, 1, 2});
  auto& indexes = filter.core_indexes();
  EXPECT_EQ(indexes.size(), 3u);
  EXPECT_EQ(indexes[0], 1u);
  EXPECT_EQ(indexes[1], 2u);
  EXPECT_EQ(indexes[2], 3u);
}

TEST_F(CATEGORY, topology_filter_set_group_indexes) {
  tmc::topology::topology_filter filter;
  filter.set_group_indexes({2, 0, 1});
  auto& indexes = filter.group_indexes();
  EXPECT_EQ(indexes.size(), 3u);
  EXPECT_EQ(indexes[0], 0u);
  EXPECT_EQ(indexes[1], 1u);
  EXPECT_EQ(indexes[2], 2u);
}

TEST_F(CATEGORY, topology_filter_set_numa_indexes) {
  tmc::topology::topology_filter filter;
  filter.set_numa_indexes({1, 0});
  auto& indexes = filter.numa_indexes();
  EXPECT_EQ(indexes.size(), 2u);
  EXPECT_EQ(indexes[0], 0u);
  EXPECT_EQ(indexes[1], 1u);
}

TEST_F(CATEGORY, topology_filter_set_cpu_kinds) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
  EXPECT_EQ(filter.cpu_kinds(), tmc::topology::cpu_kind::PERFORMANCE);
  filter.set_cpu_kinds(tmc::topology::cpu_kind::ALL);
  EXPECT_EQ(filter.cpu_kinds(), tmc::topology::cpu_kind::ALL);
}

TEST_F(CATEGORY, topology_filter_operator_or_cores_only) {
  auto topo = tmc::topology::query();
  if (topo.groups.size() < 1 || topo.groups[0].core_indexes.size() < 2) {
    GTEST_SKIP() << "Topology has fewer than 2 cores in group 0";
  }

  tmc::topology::topology_filter f1;
  f1.set_core_indexes({topo.groups[0].core_indexes[0]});

  tmc::topology::topology_filter f2;
  f2.set_core_indexes({topo.groups[0].core_indexes[1]});

  auto combined = f1 | f2;
  EXPECT_EQ(
    combined.core_indexes(),
    (std::vector<size_t>{
      topo.groups[0].core_indexes[0], topo.groups[0].core_indexes[1]
    })
  );
  EXPECT_TRUE(combined.group_indexes().empty());
  EXPECT_TRUE(combined.numa_indexes().empty());
}

TEST_F(CATEGORY, topology_filter_operator_or) {
  tmc::topology::topology_filter f1;
  f1.set_core_indexes({0});
  f1.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);

  tmc::topology::topology_filter f2;
  f2.set_group_indexes({0});
  f2.set_numa_indexes({0});
  f2.set_cpu_kinds(tmc::topology::cpu_kind::EFFICIENCY1);

  auto topo = tmc::topology::query();

  auto combined = f1 | f2;
  EXPECT_EQ(combined.core_indexes(), topo.groups[0].core_indexes);
  EXPECT_TRUE(combined.group_indexes().empty());
  EXPECT_TRUE(combined.numa_indexes().empty());
  EXPECT_EQ(
    combined.cpu_kinds(),
    tmc::topology::cpu_kind::PERFORMANCE | tmc::topology::cpu_kind::EFFICIENCY1
  );
}

TEST_F(CATEGORY, cpu_topology_is_hybrid) {
  tmc::topology::cpu_topology topo;
  topo.cpu_kind_counts = {8};
  EXPECT_FALSE(topo.is_hybrid());
  topo.cpu_kind_counts = {6, 8};
  EXPECT_TRUE(topo.is_hybrid());
}

TEST_F(CATEGORY, cpu_topology_pu_count) {
  tmc::topology::cpu_topology topo;
  tmc::topology::core_group g1;
  g1.core_indexes = {0, 1, 2, 3};
  g1.smt_level = 2;
  tmc::topology::core_group g2;
  g2.core_indexes = {4, 5};
  g2.smt_level = 1;
  topo.groups = {g1, g2};
  EXPECT_EQ(topo.pu_count(), 10u);
}

TEST_F(CATEGORY, cpu_topology_core_count) {
  tmc::topology::cpu_topology topo;
  tmc::topology::core_group g1;
  g1.core_indexes = {0, 1, 2, 3};
  tmc::topology::core_group g2;
  g2.core_indexes = {4, 5, 6, 7};
  topo.groups = {g1, g2};
  EXPECT_EQ(topo.core_count(), 8u);
}

TEST_F(CATEGORY, cpu_topology_group_count) {
  tmc::topology::cpu_topology topo;
  topo.groups.resize(4);
  EXPECT_EQ(topo.group_count(), 4u);
}

TEST_F(CATEGORY, cpu_topology_numa_count) {
  tmc::topology::cpu_topology topo;
  tmc::topology::core_group g1;
  g1.numa_index = 0;
  tmc::topology::core_group g2;
  g2.numa_index = 1;
  topo.groups = {g1, g2};
  EXPECT_EQ(topo.numa_count(), 2u);
}

TEST_F(CATEGORY, flatten_groups_flat) {
  auto groups = get_core_groups();
  auto flat = tmc::topology::detail::flatten_groups(groups);
  EXPECT_EQ(flat.size(), groups.size());
  for (size_t i = 0; i < flat.size(); ++i) {
    EXPECT_EQ(flat[i], &groups[i]);
  }
}

TEST_F(CATEGORY, flatten_groups_nested) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  tmc::topology::detail::CacheGroup parent{nullptr, -1, 0, {}, {}, 0, 0};
  parent.children.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  parent.children.push_back({nullptr, 1, 0, {}, {}, 2, 2});
  groups.push_back(parent);
  auto flat = tmc::topology::detail::flatten_groups(groups);
  EXPECT_EQ(flat.size(), 2u);
  EXPECT_EQ(flat[0]->index, 0);
  EXPECT_EQ(flat[1]->index, 1);
}

TEST_F(CATEGORY, flatten_groups_deeply_nested) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  tmc::topology::detail::CacheGroup root{nullptr, -1, 0, {}, {}, 0, 0};
  tmc::topology::detail::CacheGroup mid{nullptr, -1, 0, {}, {}, 0, 0};
  mid.children.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  mid.children.push_back({nullptr, 1, 0, {}, {}, 2, 2});
  root.children.push_back(mid);
  root.children.push_back({nullptr, 2, 0, {}, {}, 2, 4});
  groups.push_back(root);
  auto flat = tmc::topology::detail::flatten_groups(groups);
  EXPECT_EQ(flat.size(), 3u);
  EXPECT_EQ(flat[0]->index, 0);
  EXPECT_EQ(flat[1]->index, 1);
  EXPECT_EQ(flat[2]->index, 2);
}

static std::vector<tmc::topology::detail::CacheGroup>
get_core_groups_with_pus() {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  size_t coreIdx = 0;
  for (size_t i = 0; i < 4; ++i) {
    tmc::topology::detail::CacheGroup group{
      nullptr, static_cast<int>(i), 0, {}, {}, 4, i * 4
    };
    for (size_t j = 0; j < 4; ++j) {
      tmc::topology::detail::TopologyCore core;
      core.index = coreIdx++;
      core.cpuset = nullptr;
      core.cache = nullptr;
      core.numa = nullptr;
      core.cpu_kind = 0;
      core.pus = {nullptr, nullptr};
      group.cores.push_back(core);
    }
    groupedCores.push_back(group);
  }
  return groupedCores;
}

TEST_F(CATEGORY, public_group_from_private_basic) {
  auto groups = get_core_groups_with_pus();
  auto result = tmc::detail::public_group_from_private(groups[0]);
  EXPECT_EQ(result.numa_index, 0u);
  EXPECT_EQ(result.index, 0u);
  EXPECT_EQ(result.core_indexes.size(), 4u);
  EXPECT_EQ(result.core_indexes[0], 0u);
  EXPECT_EQ(result.core_indexes[1], 1u);
  EXPECT_EQ(result.core_indexes[2], 2u);
  EXPECT_EQ(result.core_indexes[3], 3u);
  EXPECT_EQ(result.cpu_kind, tmc::topology::cpu_kind::PERFORMANCE);
  EXPECT_EQ(result.smt_level, 2u);
}

TEST_F(CATEGORY, public_group_from_private_different_cpu_kind) {
  auto groups = get_core_groups_with_pus();
  groups[1].cpu_kind = 1;
  auto result = tmc::detail::public_group_from_private(groups[1]);
  EXPECT_EQ(result.cpu_kind, tmc::topology::cpu_kind::EFFICIENCY1);
}
#endif

#ifdef TMC_USE_HWLOC
TEST_F(CATEGORY, core_group_resize_no_change) {
  auto groupedCores = get_core_groups();
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);
  auto sz = flatGroups.size();

  tmc::detail::adjust_thread_groups(
    64, std::vector<float>{1.0f, 1.0f, 1.0f}, flatGroups,
    tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::FAN
  );
  EXPECT_EQ(flatGroups.size(), sz);
}

TEST_F(CATEGORY, adjust_thread_groups_pack_reduce) {
  auto groupedCores = get_core_groups();
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    8, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 8u);
}

TEST_F(CATEGORY, adjust_thread_groups_fan_reduce) {
  auto groupedCores = get_core_groups();
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    8, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::FAN
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 8u);
}

TEST_F(CATEGORY, adjust_thread_groups_occupancy) {
  auto groupedCores = get_core_groups();
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    0, std::vector<float>{0.5f, 0.5f, 0.5f}, flatGroups,
    tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_LE(total, 32u);
}

TEST_F(CATEGORY, adjust_thread_groups_increase_threads) {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 2; ++i) {
    tmc::topology::detail::CacheGroup group{
      nullptr, static_cast<int>(i), 0, {}, {}, 2, i * 2
    };
    for (size_t j = 0; j < 2; ++j) {
      tmc::topology::detail::TopologyCore core;
      core.index = i * 2 + j;
      core.cpuset = nullptr;
      core.cache = nullptr;
      core.numa = nullptr;
      core.cpu_kind = 0;
      core.pus = {nullptr, nullptr, nullptr, nullptr};
      group.cores.push_back(core);
    }
    groupedCores.push_back(group);
  }
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    16, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 16u);
}

TEST_F(CATEGORY, adjust_thread_groups_filter_by_core) {
  auto groupedCores = get_core_groups_with_pus();
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::topology::topology_filter filter;
  filter.set_core_indexes({0, 1, 4, 5, 8, 9, 12, 13});

  tmc::detail::adjust_thread_groups(
    0, std::vector<float>{}, flatGroups, filter,
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 8u);
}

TEST_F(CATEGORY, pin_thread_basic) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::ALL);
  tmc::topology::pin_thread(filter);
}

TEST_F(CATEGORY, topology_efficiency2_cpu_kind) {
  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::EFFICIENCY2);
  EXPECT_EQ(filter.cpu_kinds(), tmc::topology::cpu_kind::EFFICIENCY2);
}

TEST_F(CATEGORY, adjust_thread_groups_filter_by_group) {
  auto groupedCores = get_core_groups_with_pus();
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::topology::topology_filter filter;
  filter.set_group_indexes({0, 2});

  tmc::detail::adjust_thread_groups(
    0, std::vector<float>{}, flatGroups, filter,
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 8u);
}

TEST_F(CATEGORY, adjust_thread_groups_filter_by_numa) {
  auto groupedCores = get_core_groups_with_pus();
  for (size_t i = 0; i < groupedCores.size(); ++i) {
    for (auto& core : groupedCores[i].cores) {
      core.numa = nullptr;
    }
  }
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::topology::topology_filter filter;
  filter.set_numa_indexes({0});

  tmc::detail::adjust_thread_groups(
    0, std::vector<float>{}, flatGroups, filter,
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 16u);
}

TEST_F(CATEGORY, adjust_thread_groups_filter_by_cpu_kind) {
  auto groupedCores = get_core_groups_with_pus();
  groupedCores[2].cpu_kind = 1;
  groupedCores[3].cpu_kind = 1;
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::topology::topology_filter filter;
  filter.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);

  tmc::detail::adjust_thread_groups(
    0, std::vector<float>{}, flatGroups, filter,
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 8u);
}

TEST_F(CATEGORY, lattice_matrix_nested_groups) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  tmc::topology::detail::CacheGroup parent{nullptr, -1, 0, {}, {}, 0, 0};
  parent.children.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  parent.children.push_back({nullptr, 1, 0, {}, {}, 2, 2});
  groups.push_back(parent);
  auto matrix = tmc::detail::get_lattice_matrix(groups);
  EXPECT_EQ(matrix.size(), 16u);
  for (size_t row = 0; row < 4; ++row) {
    EXPECT_EQ(matrix[row * 4], row);
  }
}

TEST_F(CATEGORY, hierarchical_matrix_nested_groups) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  tmc::topology::detail::CacheGroup parent{nullptr, -1, 0, {}, {}, 0, 0};
  parent.children.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  parent.children.push_back({nullptr, 1, 0, {}, {}, 2, 2});
  groups.push_back(parent);
  auto matrix = tmc::detail::get_hierarchical_matrix(groups);
  EXPECT_EQ(matrix.size(), 16u);
  for (size_t row = 0; row < 4; ++row) {
    EXPECT_EQ(matrix[row * 4], row);
  }
}

TEST_F(CATEGORY, adjust_thread_groups_increase_beyond_smt) {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 2; ++i) {
    tmc::topology::detail::CacheGroup group{
      nullptr, static_cast<int>(i), 0, {}, {}, 1, i
    };
    tmc::topology::detail::TopologyCore core;
    core.index = i;
    core.cpuset = nullptr;
    core.cache = nullptr;
    core.numa = nullptr;
    core.cpu_kind = 0;
    core.pus = {nullptr, nullptr};
    group.cores.push_back(core);
    groupedCores.push_back(group);
  }
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    8, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );
  size_t total = 0;
  for (auto* g : flatGroups) {
    total += g->group_size;
  }
  EXPECT_EQ(total, 8u);
}

TEST_F(CATEGORY, lattice_matrix_varying_group_sizes) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  groups.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  groups.push_back({nullptr, 1, 0, {}, {}, 3, 2});
  auto matrix = tmc::detail::get_lattice_matrix(groups);
  EXPECT_EQ(matrix.size(), 25u);
}

TEST_F(CATEGORY, hierarchical_matrix_varying_group_sizes) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  groups.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  groups.push_back({nullptr, 1, 0, {}, {}, 3, 2});
  auto matrix = tmc::detail::get_hierarchical_matrix(groups);
  EXPECT_EQ(matrix.size(), 25u);
}

TEST_F(CATEGORY, adjust_thread_groups_smt_skips_empty_groups) {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 4; ++i) {
    tmc::topology::detail::CacheGroup group{nullptr, static_cast<int>(i), 0, {},
                                            {},      (i == 0) ? 0u : 1u,  i};
    tmc::topology::detail::TopologyCore core;
    core.index = i;
    core.cpuset = nullptr;
    core.cache = nullptr;
    core.numa = nullptr;
    core.cpu_kind = 0;
    core.pus = {nullptr, nullptr};
    group.cores.push_back(core);
    groupedCores.push_back(group);
  }
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    4, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );
  EXPECT_EQ(flatGroups[0]->group_size, 0u);
  EXPECT_EQ(flatGroups[1]->group_size, 2u);
  EXPECT_EQ(flatGroups[2]->group_size, 1u);
  EXPECT_EQ(flatGroups[3]->group_size, 1u);
}

TEST_F(CATEGORY, adjust_thread_groups_beyond_smt_skips_empty_groups) {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 4; ++i) {
    tmc::topology::detail::CacheGroup group{nullptr, static_cast<int>(i), 0, {},
                                            {},      (i == 0) ? 0u : 1u,  i};
    tmc::topology::detail::TopologyCore core;
    core.index = i;
    core.cpuset = nullptr;
    core.cache = nullptr;
    core.numa = nullptr;
    core.cpu_kind = 0;
    core.pus = {nullptr, nullptr};
    group.cores.push_back(core);
    groupedCores.push_back(group);
  }
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    7, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );
  EXPECT_EQ(flatGroups[0]->group_size, 0u);
  EXPECT_EQ(flatGroups[1]->group_size, 3u);
  EXPECT_EQ(flatGroups[2]->group_size, 2u);
  EXPECT_EQ(flatGroups[3]->group_size, 2u);
}

TEST_F(CATEGORY, adjust_thread_groups_pack_shrinks_later_groups_first) {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 3; ++i) {
    tmc::topology::detail::CacheGroup group{
      nullptr, static_cast<int>(i), 0, {}, {}, 0, i * 4
    };
    for (size_t j = 0; j < 4; ++j) {
      tmc::topology::detail::TopologyCore core;
      core.index = i * 4 + j;
      core.cpuset = nullptr;
      core.cache = nullptr;
      core.numa = nullptr;
      core.cpu_kind = 0;
      core.pus = {nullptr};
      group.cores.push_back(core);
    }
    groupedCores.push_back(group);
  }
  groupedCores[0].group_size = 0;
  groupedCores[1].group_size = 4;
  groupedCores[2].group_size = 2;
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    4, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::PACK
  );
  EXPECT_EQ(flatGroups[0]->group_size, 0u);
  EXPECT_EQ(flatGroups[1]->group_size, 4u);
  EXPECT_EQ(flatGroups[2]->group_size, 0u);
}

TEST_F(CATEGORY, adjust_thread_groups_fan_shrinks_earlier_groups_first) {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 3; ++i) {
    tmc::topology::detail::CacheGroup group{
      nullptr, static_cast<int>(i), 0, {}, {}, (i == 0) ? 4u : 2u, i * 4
    };
    for (size_t j = 0; j < 4; ++j) {
      tmc::topology::detail::TopologyCore core;
      core.index = i * 4 + j;
      core.cpuset = nullptr;
      core.cache = nullptr;
      core.numa = nullptr;
      core.cpu_kind = 0;
      core.pus = {nullptr};
      group.cores.push_back(core);
    }
    groupedCores.push_back(group);
  }
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    2, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::FAN
  );
  EXPECT_EQ(flatGroups[0]->group_size, 1u);
  EXPECT_EQ(flatGroups[1]->group_size, 1u);
  EXPECT_EQ(flatGroups[2]->group_size, 0u);
}

TEST_F(CATEGORY, adjust_thread_groups_fan_shrinks_larger_groups_first) {
  std::vector<tmc::topology::detail::CacheGroup> groupedCores;
  for (size_t i = 0; i < 3; ++i) {
    tmc::topology::detail::CacheGroup group{
      nullptr, static_cast<int>(i), 0, {}, {}, (i == 0) ? 4u : 2u, i * 4
    };
    for (size_t j = 0; j < 4; ++j) {
      tmc::topology::detail::TopologyCore core;
      core.index = i * 4 + j;
      core.cpuset = nullptr;
      core.cache = nullptr;
      core.numa = nullptr;
      core.cpu_kind = 0;
      core.pus = {nullptr};
      group.cores.push_back(core);
    }
    groupedCores.push_back(group);
  }
  auto flatGroups = tmc::topology::detail::flatten_groups(groupedCores);

  tmc::detail::adjust_thread_groups(
    6, std::vector<float>{}, flatGroups, tmc::topology::topology_filter{},
    tmc::topology::thread_packing_strategy::FAN
  );
  EXPECT_EQ(flatGroups[0]->group_size, 2u);
  EXPECT_EQ(flatGroups[1]->group_size, 2u);
  EXPECT_EQ(flatGroups[2]->group_size, 2u);
}

TEST_F(CATEGORY, work_stealing_matrix_deeply_nested_children) {
  // This is really testing the behavior of WorkStealingMatrixIterator,
  // but uses get_lattice_matrix to do it.
  // Create a hierarchy with leaves at different depths to exercise both
  // "child is deeper" and "child is higher" branches in
  // WorkStealingMatrixIterator::get_group_order()
  std::vector<tmc::topology::detail::CacheGroup> groups;
  tmc::topology::detail::CacheGroup left_mid{nullptr, -1, 0, {}, {}, 0, 0};
  left_mid.children.push_back({nullptr, 0, 0, {}, {}, 1, 0});

  tmc::topology::detail::CacheGroup right_bottom{nullptr, -1, 0, {}, {}, 0, 0};
  right_bottom.children.push_back({nullptr, 1, 0, {}, {}, 1, 1});

  tmc::topology::detail::CacheGroup right_mid{nullptr, -1, 0, {}, {}, 0, 0};
  right_mid.children.push_back(right_bottom);

  tmc::topology::detail::CacheGroup root{nullptr, -1, 0, {}, {}, 0, 0};
  root.children.push_back(left_mid);
  root.children.push_back(right_mid);
  groups.push_back(root);

  auto matrix = tmc::detail::get_lattice_matrix(groups);
  EXPECT_EQ(matrix.size(), 4u);
  EXPECT_EQ(matrix[0], 0);
  EXPECT_EQ(matrix[1], 1);
  EXPECT_EQ(matrix[2], 1);
  EXPECT_EQ(matrix[3], 0);
}

TEST_F(CATEGORY, lattice_matrix_skips_empty_groups) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  groups.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  groups.push_back({nullptr, 1, 0, {}, {}, 0, 2});
  groups.push_back({nullptr, 2, 0, {}, {}, 2, 2});

  auto matrix = tmc::detail::get_lattice_matrix(groups);
  EXPECT_EQ(matrix.size(), 16u);
  for (size_t row = 0; row < 4; ++row) {
    EXPECT_EQ(matrix[row * 4], row);
  }
  for (size_t row = 0; row < 4; ++row) {
    std::vector<bool> seen(4, false);
    for (size_t col = 0; col < 4; ++col) {
      size_t val = matrix[row * 4 + col];
      EXPECT_LT(val, 4u);
      seen[val] = true;
    }
    for (size_t i = 0; i < 4; ++i) {
      EXPECT_TRUE(seen[i]) << "Thread " << i << " missing from row " << row;
    }
  }
}

TEST_F(CATEGORY, hierarchical_matrix_skips_empty_groups) {
  std::vector<tmc::topology::detail::CacheGroup> groups;
  groups.push_back({nullptr, 0, 0, {}, {}, 2, 0});
  groups.push_back({nullptr, 1, 0, {}, {}, 0, 2});
  groups.push_back({nullptr, 2, 0, {}, {}, 2, 2});

  auto matrix = tmc::detail::get_hierarchical_matrix(groups);
  EXPECT_EQ(matrix.size(), 16u);
  for (size_t row = 0; row < 4; ++row) {
    EXPECT_EQ(matrix[row * 4], row);
  }
  for (size_t row = 0; row < 4; ++row) {
    std::vector<bool> seen(4, false);
    for (size_t col = 0; col < 4; ++col) {
      size_t val = matrix[row * 4 + col];
      EXPECT_LT(val, 4u);
      seen[val] = true;
    }
    for (size_t i = 0; i < 4; ++i) {
      EXPECT_TRUE(seen[i]) << "Thread " << i << " missing from row " << row;
    }
  }
}
#endif

#undef CATEGORY

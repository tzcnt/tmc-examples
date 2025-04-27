// Various tests to increase code coverage in specific areas that are otherwise
// not exercised.
#include "test_common.hpp"
#include "tmc/detail/thread_layout.hpp"

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
  tmc::detail::get_group_iteration_order(16, 5);
}

std::vector<tmc::detail::L3CacheSet> get_core_groups() {
  std::vector<tmc::detail::L3CacheSet> groupedCores;
  for (size_t i = 0; i < 16; ++i) {
    groupedCores.push_back(
      tmc::detail::L3CacheSet{nullptr, 4, std::vector<size_t>{}}
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

#ifdef TMC_USE_HWLOC
TEST_F(CATEGORY, core_group_resize_no_change) {
  auto groupedCores = get_core_groups();
  auto sz = groupedCores.size();
  bool lasso = false;
  tmc::detail::adjust_thread_groups(64, 0.0f, groupedCores, lasso);
  EXPECT_EQ(lasso, true);
  EXPECT_EQ(groupedCores.size(), sz);
}
#endif

#undef CATEGORY

#include "test_common.hpp"
#include "tmc/current.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_ex_cpu

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

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
    1
  )
    .wait();
}

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

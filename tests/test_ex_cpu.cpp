#include "test_common.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_ex_cpu

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

#include "test_executors.ipp"
#include "test_nested_executors.ipp"
#include "test_spawn_composition.ipp"
#include "test_spawn_func_many.ipp"
#include "test_spawn_func_many_detach.ipp"
#include "test_spawn_func_many_each.ipp"
#include "test_spawn_func_many_run_early.ipp"
#include "test_spawn_many.ipp"
#include "test_spawn_many_detach.ipp"
#include "test_spawn_many_each.ipp"
#include "test_spawn_many_run_early.ipp"
#include "test_spawn_tuple.ipp"

#undef CATEGORY

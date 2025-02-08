#include "test_common.hpp"
#include "tmc/ex_cpu.hpp"

#include <gtest/gtest.h>
#include <optional>

#define CATEGORY test_ex_braid

class CATEGORY : public testing::Test {
protected:
  static inline std::optional<tmc::ex_braid> braid;
  static void SetUpTestSuite() {
    tmc::cpu_executor().init();
    braid.emplace(tmc::cpu_executor());
  }

  static void TearDownTestSuite() {
    braid.reset();
    tmc::cpu_executor().teardown();
  }

  static tmc::ex_braid& ex() { return *braid; }
};

#include "test_executors.ipp"
#include "test_spawn_many.ipp"
#include "test_spawn_many_detach.ipp"
#include "test_spawn_many_each.ipp"
#include "test_spawn_many_run_early.ipp"
#include "test_spawn_tuple.ipp"

#undef CATEGORY
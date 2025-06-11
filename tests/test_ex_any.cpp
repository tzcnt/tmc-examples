#include "test_common.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_ex_any

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_any& ex() { return *tmc::cpu_executor().type_erased(); }
};

#include "test_executors.ipp"

#undef CATEGORY

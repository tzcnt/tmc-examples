#include "test_common.hpp"

#include <gtest/gtest.h>
#include <ranges>
#include <thread>

#define CATEGORY test_ex_cpu

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

#include "test_executors.ipp"

#undef CATEGORY
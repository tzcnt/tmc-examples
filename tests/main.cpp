#include <cstdio>

#include "gtest/gtest.h"

// class Environment : public ::testing::Environment {
// public:
//   ~Environment() override {}

//   // Override this to define how to set up the environment.
//   void SetUp() override { tmc::cpu_executor().init(); }

//   // Override this to define how to tear down the environment.
//   void TearDown() override { tmc::cpu_executor().teardown(); }
// };

int main(int argc, char** argv) {
  // GTEST_FLAG_SET(death_test_style, "threadsafe");
  //::testing::AddGlobalTestEnvironment(new Environment);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

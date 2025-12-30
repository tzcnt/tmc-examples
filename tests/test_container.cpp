// Standalone tests for container CPU detection.
// These tests are designed to be run inside Docker containers with different
// CPU limit configurations.
//
// Environment variable TMC_CONTAINER_TEST controls which test to run:
// - "unlimited" (or unset): Expects container with no CPU limit
// - "cpu_quota": Expects container with --cpus limit (detected by cgroups)
// - "cpuset": Expects container with --cpuset-cpus limit (detected by hwloc)

// The "unlimited" test is the only one that should run outside of a container.
// Run the docker_tests.sh script to execute the other tests.

#include "tmc/detail/container_cpu_quota.hpp"
#include "tmc/ex_cpu.hpp"

#ifdef TMC_USE_HWLOC
#include "tmc/topology.hpp"
#endif

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <string>

static std::string safe_getenv(const char* name) {
#ifdef _WIN32
  char* buf = nullptr;
  size_t len = 0;
  if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
    std::string result(buf);
    free(buf);
    return result;
  }
  return "unlimited";
#else
  const char* env = std::getenv(name);
  return env ? std::string(env) : "unlimited";
#endif
}

static std::string get_test_mode() { return safe_getenv("TMC_CONTAINER_TEST"); }

#define CATEGORY test_container

class CATEGORY : public testing::Test {};

TEST_F(CATEGORY, unlimited) {
  if (get_test_mode() != "unlimited") {
    GTEST_SKIP();
  }

  auto quota = tmc::detail::query_container_cpu_quota();
  EXPECT_FALSE(quota.is_container_limited());
  EXPECT_EQ(quota.status, tmc::detail::container_cpu_status::UNLIMITED);
}

TEST_F(CATEGORY, cpu_quota) {
  if (get_test_mode() != "cpu_quota") {
    GTEST_SKIP();
  }

  auto quota = tmc::detail::query_container_cpu_quota();
  EXPECT_TRUE(quota.is_container_limited());
  EXPECT_EQ(quota.status, tmc::detail::container_cpu_status::LIMITED);
  EXPECT_GT(quota.cpu_count, 0.0);

  // With --cpus=1.5, we expect approximately 1.5 CPUs
  // Allow some tolerance for rounding
  std::string expected_cpus_env = safe_getenv("TMC_EXPECTED_CPUS");
  if (!expected_cpus_env.empty()) {
    double expected = std::stod(expected_cpus_env);
    EXPECT_NEAR(quota.cpu_count, expected, 0.1);
  }
}

TEST_F(CATEGORY, cpuset) {
  if (get_test_mode() != "cpuset") {
    GTEST_SKIP();
  }

#ifndef TMC_USE_HWLOC
  GTEST_SKIP();
#else
  // When --cpuset-cpus is used, hwloc should only see the allowed CPUs
  auto topo = tmc::topology::query();

  std::string expected_cpus_env = safe_getenv("TMC_EXPECTED_CPUS");
  if (!expected_cpus_env.empty()) {
    size_t expected = std::stoul(expected_cpus_env);
    EXPECT_EQ(topo.pu_count(), expected);
  }

  // Container CPU quota detection may or may not detect a limit
  // depending on whether cgroups cpu quota is also set.
  // But for this test, we set only only --cpuset-cpus (no --cpus), so the
  // cgroups quota should be unlimited
  auto quota = tmc::detail::query_container_cpu_quota();

  EXPECT_FALSE(quota.is_container_limited());
  EXPECT_GT(topo.core_count(), 0u);
  EXPECT_GT(topo.pu_count(), 0u);
#endif
}

TEST_F(CATEGORY, executor_respects_container_limits) {
  std::string mode = get_test_mode();
  if (mode != "cpu_quota" && mode != "cpuset") {
    GTEST_SKIP();
  }

  // Initialize executor and verify it respects container limits
  tmc::ex_cpu executor;
  executor.init();

  size_t thread_count = executor.thread_count();
  EXPECT_GT(thread_count, 0u);

  std::string expected_cpus_env = safe_getenv("TMC_EXPECTED_CPUS");
  if (!expected_cpus_env.empty()) {
    size_t expected =
      static_cast<size_t>(std::ceil(std::stod(expected_cpus_env)));
    EXPECT_LE(thread_count, expected);
  }

  executor.teardown();
}

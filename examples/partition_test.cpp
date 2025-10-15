// Example demonstrating ex_cpu partitioning functionality

#define TMC_IMPL
#include "tmc/ex_cpu.hpp"

#include <cstdio>
#include <iostream>

tmc::task<int> example() {
#ifdef TMC_USE_HWLOC
  std::printf("=== Testing partition functionality ===\n");

  // Test 1: Create an executor using all resources (default)
  std::printf("\n=== Test 1: Default (all resources) ===\n");
  tmc::ex_cpu exec1;
  exec1.init();
  std::printf("Created executor with %zu threads\n", exec1.thread_count());
  exec1.teardown();

  // Test 2: Query topology and test L3 partitioning
  auto topology = tmc::query_system_topology();
  std::printf("\nSystem has %zu L3 groups\n", topology.l3_groups.size());

  if (topology.l3_groups.size() >= 2) {
    std::printf("\n=== Test 2: Partition to L3 group 0 ===\n");
    tmc::ex_cpu exec2;
    exec2.set_partition_l3({0});
    exec2.init();
    std::printf("Created executor with %zu threads\n", exec2.thread_count());
    exec2.teardown();
  }

  std::printf("\n=== All tests completed successfully ===\n");
#else
  std::printf(
    "This example requires TMC_USE_HWLOC to be enabled.\n"
    "Please rebuild with -DTMC_USE_HWLOC=ON\n");
#endif

  co_return 0;
}

int main() { return tmc::async_main(example()); }

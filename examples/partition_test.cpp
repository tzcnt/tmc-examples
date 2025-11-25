// Example demonstrating ex_cpu partitioning functionality

#define TMC_IMPL
#include "tmc/ex_cpu.hpp"

#include <cstdio>
#include <memory>
#include <vector>

tmc::task<int> example() {
#ifdef TMC_USE_HWLOC
  std::printf("=== Testing partition functionality ===\n");

  // Test 1: Create an executor using all resources (default)
  std::printf("\n=== Test 1: Default (all resources) ===\n");
  tmc::ex_cpu exec1;
  exec1.init();
  std::printf("Created executor with %zu threads\n", exec1.thread_count());
  exec1.teardown();

  // Test 2: Query topology and create an executor for each L3 partition
  auto topology = tmc::query_system_topology();
  std::printf("\nSystem has %zu L3 groups\n", topology.l3_groups.size());
  
  // Display heterogeneous core information
  if (topology.has_hybrid_cores) {
    std::printf("Hybrid CPU detected:\n");
    std::printf("  Performance cores: %zu\n", topology.performance_core_count);
    std::printf("  Efficiency cores: %zu\n", topology.efficiency_core_count);
  } else {
    std::printf("Homogeneous CPU: %zu cores\n", 
                topology.performance_core_count);
  }

  // Test 3: CPU kind partitioning (if hybrid cores exist)
  if (topology.has_hybrid_cores) {
    std::printf("\n=== Test 3: Partition by CPU kind ===\n");
    
    {
      std::printf("Performance cores: ");
      std::fflush(stdout);
      auto exec = std::make_unique<tmc::ex_cpu>();
      exec->set_partition_cpukind(tmc::ex_cpu::CpuKind::Performance);
      exec->init();
      std::printf("Created executor with %zu threads\n", exec->thread_count());
      exec->teardown();
    }
    
    {
      std::printf("Efficiency cores: ");
      std::fflush(stdout);
      auto exec = std::make_unique<tmc::ex_cpu>();
      exec->set_partition_cpukind(tmc::ex_cpu::CpuKind::Efficiency);
      exec->init();
      std::printf("Created executor with %zu threads\n", exec->thread_count());
      exec->teardown();
    }
  }

  if (topology.l3_groups.size() > 0) {
    std::printf("\n=== Test 4: Create executor for each L3 partition ===\n");
    
    // Create and teardown each executor sequentially
    for (size_t i = 0; i < topology.l3_groups.size(); ++i) {
      std::printf("L3 group %zu: ", i);
      std::fflush(stdout);
      auto exec = std::make_unique<tmc::ex_cpu>();
      exec->set_partition_l3({static_cast<unsigned>(i)});
      exec->init();
      std::printf("Created executor with %zu threads\n", exec->thread_count());
      exec->teardown();
    }
    std::printf("Successfully created and tore down %zu partitioned executors\n",
                topology.l3_groups.size());
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

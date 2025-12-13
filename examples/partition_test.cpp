// Example demonstrating ex_cpu partitioning functionality

#define TMC_IMPL
#include "tmc/ex_cpu.hpp"
#include "tmc/topology.hpp"

#include <cstdio>
#include <memory>
#include <vector>

static tmc::task<int> example() {
#ifdef TMC_USE_HWLOC
  std::printf("=== Testing partition functionality ===\n");

  // Test 1: Create an executor using all resources (default)
  std::printf("\n=== Test 1: Default (all resources) ===\n");
  tmc::ex_cpu exec1;
  exec1.init();
  std::printf("Created executor with %zu threads\n", exec1.thread_count());
  exec1.teardown();

  // Test 2: Query topology and create an executor for each L3 partition
  auto topology = tmc::topology::query();
  std::printf("\nSystem has %zu cache groups\n", topology.groups.size());

  // Display heterogeneous core information
  if (topology.is_hybrid()) {
    std::printf("Hybrid CPU detected:\n");
    std::printf("  Performance cores: %zu\n", topology.cpu_kind_counts[0]);
    std::printf("  Efficiency cores: %zu\n", topology.cpu_kind_counts[1]);
  } else {
    std::printf("Homogeneous CPU: %zu cores\n", topology.cpu_kind_counts[0]);
  }

  // Test 3: CPU kind partitioning (if hybrid cores exist)
  if (topology.is_hybrid()) {
    std::printf("\n=== Test 3: Partition by CPU kind ===\n");

    {
      std::printf("Performance cores: \n");
      std::fflush(stdout);
      tmc::topology::TopologyFilter f;
      f.set_cpu_kinds(tmc::topology::CpuKind::PERFORMANCE);
      auto exec = std::make_unique<tmc::ex_cpu>();
      exec->set_topology_filter(f).init();
      std::printf("Created executor with %zu threads\n", exec->thread_count());
      exec->teardown();
    }

    {
      std::printf("Efficiency cores: \n");
      std::fflush(stdout);
      tmc::topology::TopologyFilter f;
      f.set_cpu_kinds(tmc::topology::CpuKind::EFFICIENCY1);
      auto exec = std::make_unique<tmc::ex_cpu>();
      exec->set_topology_filter(f).init();
      std::printf("Created executor with %zu threads\n", exec->thread_count());
      exec->teardown();
    }
  }

  if (topology.groups.size() > 0) {
    std::printf("\n=== Test 4: Create executor for each cache ===\n");

    // Create and teardown each executor sequentially
    for (size_t i = 0; i < topology.groups.size(); ++i) {
      std::printf("cache group %zu: \n", i);
      std::fflush(stdout);
      tmc::topology::TopologyFilter f;
      f.set_group_indexes({0});
      auto exec = std::make_unique<tmc::ex_cpu>();
      exec->set_topology_filter(f).init();
      std::printf("Created executor with %zu threads\n", exec->thread_count());
      exec->teardown();
    }
    std::printf(
      "Successfully created and tore down %zu partitioned executors\n",
      topology.groups.size()
    );
  }

  std::printf("\n=== All tests completed successfully ===\n");
#else
  std::printf(
    "This example requires TMC_USE_HWLOC to be enabled.\n"
    "Please rebuild with -DTMC_USE_HWLOC=ON\n"
  );
#endif

  co_return 0;
}

int main() { return tmc::async_main(example()); }

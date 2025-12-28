// Example demonstrating ex_cpu partitioning functionality

#define TMC_IMPL
#include "tmc/all_headers.hpp"

#include <cstdio>
#include <string>

inline thread_local std::string thread_name{};

#ifndef TMC_USE_HWLOC
int main() {
  std::printf("This example requires TMC_USE_HWLOC to be enabled.\n");
}
#else

static void print_location() {
  std::printf(
    "Hello from priority %zu, running on %s core\n", tmc::current_priority(),
    thread_name.c_str()
  );
}

int main() {
  auto topo = tmc::topology::query();

  if (topo.is_hybrid()) {
    std::printf("Hybrid CPU detected:\n");
    std::printf("  Performance cores: %zu\n", topo.cpu_kind_counts[0]);
    std::printf("  Efficiency cores: %zu\n", topo.cpu_kind_counts[1]);
    // Configure P-cores to take priority 0 (high priority)
    // and E-cores to take priority 1 (low priority)
    tmc::topology::topology_filter p_cores;
    p_cores.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
    tmc::topology::topology_filter e_cores;
    e_cores.set_cpu_kinds(tmc::topology::cpu_kind::EFFICIENCY1);
    tmc::cpu_executor()
      .add_partition(p_cores, 0, 1)
      .add_partition(e_cores, 1, 2)
      .set_priority_count(2);
  } else {
    std::printf("Homogeneous CPU: %zu cores\n", topo.cpu_kind_counts[0]);
    // Priority 0 and 1 will run on any core
    tmc::cpu_executor().set_priority_count(2);
    // Optionally, you could set_priority_count(1) here, so that on a
    // homogeneous processor, all work runs at the same priority. If work is
    // submitted at priority 1, it would be clamped to 0.
  }

  tmc::cpu_executor()
    .set_thread_init_hook([](tmc::topology::thread_info info) {
      thread_name = info.group.cpu_kind == tmc::topology::cpu_kind::PERFORMANCE
                      ? "performance"
                      : "efficiency";
    })
    .init();

  std::printf("\nrunning high priority task...\n");
  tmc::post_waitable(tmc::cpu_executor(), print_location, 0).wait();
  std::printf("\nrunning low priority task...\n");
  tmc::post_waitable(tmc::cpu_executor(), print_location, 1).wait();
}

#endif

// Prints the CPU topology as TMC views it.
// This topology is based on the hwloc topology, but removes all unnecessary
// data, and creates groups based on shared caches and CPU kinds.

#define TMC_IMPL

#include "tmc/topology.hpp"
#include <cstdio>

#ifndef TMC_USE_HWLOC
int main() {
  std::printf("This examples requires TMC_USE_HWLOC to be enabled.\n");
}
#else
int main() {
  auto topo = tmc::topology::query();
  std::printf("# of NUMA nodes         : %zu\n", topo.numa_count());
  std::printf("# of physical processors: %zu\n", topo.core_count());
  std::printf("# of logical processors : %zu\n", topo.pu_count());
  std::printf("\n");

  std::printf(
    "Hybrid architecture?    : %s\n", topo.is_hybrid() ? "true" : "false"
  );
  std::printf("# of CPU kinds          : %zu\n", topo.cpu_kind_counts.size());
  std::printf("# of PERFORMANCE cores  : %zu\n", topo.cpu_kind_counts[0]);
  if (topo.cpu_kind_counts.size() > 1) {
    std::printf("# of EFFICIENCY1 cores  : %zu\n", topo.cpu_kind_counts[1]);
  }
  if (topo.cpu_kind_counts.size() > 2) {
    std::printf("# of EFFICIENCY2 cores  : %zu\n", topo.cpu_kind_counts[2]);
  }
  std::printf("\n");

  std::printf("# of core groups        : %zu\n", topo.group_count());
  for (auto& group : topo.groups) {
    std::printf("Group %zu:\n", group.index);
    std::printf("  NUMA index            : %zu\n", group.numa_index);
    if (group.cpu_kind == tmc::topology::cpu_kind::PERFORMANCE) {
      std::printf("  CPU kind              : PERFORMANCE\n");
    } else if (group.cpu_kind == tmc::topology::cpu_kind::EFFICIENCY1) {
      std::printf("  CPU kind              : EFFICIENCY1\n");
    } else {
      std::printf("  CPU kind              : EFFICIENCY2\n");
    }
    std::printf("  SMT level             : %zu\n", group.smt_level);
    std::printf("  physical core indexes : ");
    for (auto& idx : group.core_indexes) {
      std::printf("%zu ", idx);
    }
    std::printf("\n");
  }
}
#endif

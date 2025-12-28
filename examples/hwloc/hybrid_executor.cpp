// Demonstrates how to create an executor that will steer work between P- and
// E-cores on a hybrid machine based on priority.
// On a homogeneous machine, it sends all work to the same executor.

#define TMC_IMPL
#include "tmc/all_headers.hpp"
#include "tmc/utils.hpp"

#include <cstdio>
#include <ranges>

#ifndef TMC_USE_HWLOC
int main() {
  std::printf("This example requires TMC_USE_HWLOC to be enabled.\n");
}
#else

inline thread_local tmc::topology::cpu_kind::value cpuKind{};

using out_array_t = std::array<std::array<std::atomic<size_t>, 2>, 3>;

static tmc::task<void> capture_location(out_array_t& output) {
  auto prio = tmc::current_priority();
  auto kindIdx = cpuKind == tmc::topology::cpu_kind::PERFORMANCE ? 0u : 1u;
  output[prio][kindIdx]++;
  co_return;
}

static void run_tasks(out_array_t& output, size_t prio) {
  tmc::post_bulk_waitable(
    tmc::cpu_executor(),
    std::ranges::views::iota(0u, 10000u) |
      std::ranges::views::transform([&](size_t) {
        return capture_location(output);
      }),
    prio
  )
    .wait();
}

int main() {
  auto topo = tmc::topology::query();

  if (topo.is_hybrid()) {
    std::printf("Hybrid CPU detected:\n");
    std::printf("  Performance cores: %zu\n", topo.cpu_kind_counts[0]);
    std::printf("  Efficiency cores: %zu\n", topo.cpu_kind_counts[1]);

    tmc::topology::topology_filter p_cores;
    p_cores.set_cpu_kinds(tmc::topology::cpu_kind::PERFORMANCE);
    tmc::topology::topology_filter e_cores;
    e_cores.set_cpu_kinds(tmc::topology::cpu_kind::EFFICIENCY1);

    // P-cores handle high (priority 0) and medium (priority 1) work
    // E-cores handle medium (priority 1) and low (priority 2) work
    // Work stealing between core types can happen for priority 1 work
    tmc::cpu_executor()
      .add_partition(p_cores, 0, 2)
      .add_partition(e_cores, 1, 3)
      .set_priority_count(3);
    // Optionally, the priority ranges could be made non-overlapping
  } else {
    std::printf("Homogeneous CPU: %zu cores\n", topo.cpu_kind_counts[0]);
    // Any priority can run on any core
    tmc::cpu_executor().set_priority_count(3);
    // Optionally, you could set_priority_count(1) here, so that on a
    // homogeneous processor, all work runs at the same priority. If work is
    // submitted at lower priorities, it would be clamped to priority 0.
  }

  tmc::cpu_executor()
    .set_thread_init_hook([](tmc::topology::thread_info info) {
      cpuKind = info.group.cpu_kind;
    })
    .init();

  // Run tasks at each priority and capture where they ran.

  // On a hybrid CPU:
  // high priority will run exclusively on PERFORMANCE
  // medium priority will be split between PERFORMANCE and EFFICIENCY1
  // low priority will run exclusively on EFFICIENCY1

  // On a homogeneous CPU, all will run on PERFORMANCE

  std::printf("\nRunning high/medium/low priority tasks...\n\n");
  out_array_t ranOnCounts = {};
  run_tasks(ranOnCounts, 0);
  run_tasks(ranOnCounts, 1);
  run_tasks(ranOnCounts, 2);

  std::printf(
    "%zu high priority tasks ran on PERFORMANCE cores\n",
    ranOnCounts[0][0].load()
  );
  std::printf(
    "%zu high priority tasks ran on EFFICIENCY1 cores\n\n",
    ranOnCounts[0][1].load()
  );

  std::printf(
    "%zu medium priority tasks ran on PERFORMANCE cores\n",
    ranOnCounts[1][0].load()
  );
  std::printf(
    "%zu medium priority tasks ran on EFFICIENCY1 cores\n\n",
    ranOnCounts[1][1].load()
  );

  std::printf(
    "%zu low priority tasks ran on PERFORMANCE cores\n",
    ranOnCounts[2][0].load()
  );
  std::printf(
    "%zu low priority tasks ran on EFFICIENCY1 cores\n",
    ranOnCounts[2][1].load()
  );
}

#endif

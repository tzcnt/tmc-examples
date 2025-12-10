// Example demonstrating ex_cpu partitioning functionality

#define TMC_IMPL
#include "hybrid_executor.hpp"
#include "tmc/all_headers.hpp"

#include <cstdio>

static ex_hybrid hybrid_executor;

static void print_location() {
  if (tmc::current_executor() ==
      hybrid_executor.performance_executor.type_erased()) {
    std::printf(
      "Hello from performance executor, priority %zu\n", tmc::current_priority()
    );
  } else if (tmc::current_executor() ==
             hybrid_executor.efficiency_executor.type_erased()) {
    std::printf(
      "Hello from efficiency executor, priority %zu\n", tmc::current_priority()
    );
  } else {
    std::printf(
      "Hello from UNKNOWN executor, priority %zu\n", tmc::current_priority()
    );
  }
}

int main() {
#ifndef TMC_USE_HWLOC
  std::printf(
    "Hwloc is not enabled. ex_hybrid will behave as if there are only "
    "performance cores."
  )
#endif

    hybrid_executor.init();
  if (hybrid_executor.hybrid_enabled) {
    std::printf("Hybrid CPU detected:\n");
    std::printf(
      "  Performance cores: %zu\n",
      hybrid_executor.performance_executor.thread_count()
    );
    std::printf(
      "  Efficiency cores: %zu\n",
      hybrid_executor.efficiency_executor.thread_count()
    );
    std::printf("\nrunning high priority task...\n");
    tmc::post_waitable(hybrid_executor, print_location, 0).wait();
    std::printf("\nrunning low priority task...\n");
    tmc::post_waitable(hybrid_executor, print_location, 1).wait();
  } else {
    std::printf(
      "Homogeneous CPU: %zu cores\n",
      hybrid_executor.performance_executor.thread_count()
    );
    std::printf("\nrunning high priority task...\n");
    tmc::post_waitable(hybrid_executor, print_location, 0).wait();
    std::printf("\nrunning low priority task...\n");
    tmc::post_waitable(hybrid_executor, print_location, 1).wait();
  }
}

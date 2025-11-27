// An implementation of the recursive fork fibonacci parallelism test.
// This is not intended to be an efficient fibonacci calculator,
// but a test of the runtime's fork/join efficiency.

#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/detail/thread_layout.hpp"
#include "tmc/detail/tiny_vec.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <utility>

static tmc::task<size_t> fib(size_t n) {
  if (n < 2)
    co_return n;
  /* Several different ways to spawn / await 2 child tasks */

  /* Execute them one-by-one (serially) */
  // auto x = co_await fib(n - 2);
  // auto y = co_await fib(n - 1);
  // co_return x + y;

  /* Preallocated task array, bulk spawn */
  // std::array<task<size_t>, 2> tasks;
  // tasks[0] = fib(n - 2);
  // tasks[1] = fib(n - 1);
  // auto results = co_await spawn_many<2>(tasks.data());
  // co_return results[0] + results[1];

  /* Iterator adapter function to generate tasks, bulk spawn */
  /* required #include "tmc/utils.hpp"*/
  // auto results = co_await spawn_many<2>(iter_adapter(n - 2, fib));
  // co_return results[0] + results[1];

  /* You could also use std::ranges for this */
  /* requires #include <ranges> */
  // auto results = co_await spawn_many<2>((std::ranges::views::iota(n - 2) |
  //                                        std::ranges::views::transform(fib))
  //                                         .begin());
  // co_return results[0] + results[1];

  /* Spawn one, then serially execute the other, then await the first */
  auto xt = spawn(fib(n - 1)).fork();
  auto y = co_await fib(n - 2);
  auto x = co_await std::move(xt);
  co_return x + y;

  /* Submit using variadic parameter pack, and get the results as a tuple */
  // auto [x, y] = co_await spawn_tuple(fib(n - 1), fib(n - 2));
  // co_return x + y;
}

static tmc::task<void> top_fib(size_t n) {
  auto result = co_await fib(n);
  std::printf("%zu\n", result);
  co_return;
}

constexpr size_t NRUNS = 1;
int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifndef NDEBUG
  // Hardcode the size in debug mode so we don't have to fuss around with
  // input arguments in the debug config.
  size_t n = 30;
#else
  if (argc != 2) {
    printf("Usage: fib <n-th fibonacci number requested>\n");
    return -1;
  }

  size_t n = static_cast<size_t>(atoi(argv[1]));
#endif
  auto topology = tmc::topology::query();
  std::printf("\nSystem has %zu L3 groups\n", topology.llc_count());

  if (topology.llc_count() < 2) {
    std::printf("\n=== Test 4: Create executor for each L3 partition ===\n");

    tmc::detail::tiny_vec<tmc::ex_cpu> execs;
    execs.resize(topology.llc_count());
    // Create and teardown each executor sequentially
    for (size_t i = 0; i < execs.size(); ++i) {
      execs.emplace_at(i);
      tmc::topology::TopologyFilter f;
      f.set_llc_indexes({i});
      execs[i].set_topology_filter(f);
      execs[i].init();
    }
    auto startTime = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> futs;
    futs.resize(execs.size());
    for (size_t i = 0; i < execs.size(); ++i) {
      futs[i] = tmc::post_waitable(execs[i], top_fib(n));
    }
    for (size_t i = 0; i < execs.size(); ++i) {
      futs[i].wait();
    }
    auto endTime = std::chrono::high_resolution_clock::now();
    size_t totalTimeUs = static_cast<size_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
        .count()
    );
    std::printf("%zu us (effective)\n", totalTimeUs / (NRUNS * execs.size()));
    execs.clear();
  } else {
    tmc::topology::TopologyFilter f;
    f.set_p_e_cores(false);
    f.set_core_indexes({0, 17, 35, 63});
    f.set_llc_indexes({0, 8, 15});
    f.set_numa_indexes({0});
    tmc::cpu_executor()
      .set_topology_filter(f)
      .set_thread_occupancy(1.5f)
      .init();
    std::printf("exec has %zu threads\n", tmc::cpu_executor().thread_count());
    tmc::async_main([](size_t N) -> tmc::task<int> {
      auto startTime = std::chrono::high_resolution_clock::now();
      for (size_t i = 0; i < NRUNS; ++i) {
        co_await top_fib(N);
      }

      auto endTime = std::chrono::high_resolution_clock::now();
      size_t totalTimeUs = static_cast<size_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
          endTime - startTime
        )
          .count()
      );
      std::printf("%zu us\n", totalTimeUs / NRUNS);
      co_return 0;
    }(n));
  }
}

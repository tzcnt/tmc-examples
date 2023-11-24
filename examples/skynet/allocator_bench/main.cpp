#define TMC_IMPL

#include "build/bench_config.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/task.hpp"
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <coroutine>
#include <iostream>
#include <thread>

using namespace tmc;

namespace skynet {
namespace coro {
namespace bulk {
std::atomic_bool done;
// all tasks are spawned at the same priority
template <size_t depth_max>
task<size_t> skynet_one(size_t base_num, size_t depth) {
  if (depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - depth - 1; ++i) {
    depth_offset *= 10;
  }
  std::array<task<size_t>, 10> children;
  for (size_t idx = 0; idx < 10; ++idx) {
    children[idx] =
      skynet_one<depth_max>(base_num + depth_offset * idx, depth + 1);
  }
  std::array<size_t, 10> results = co_await spawn_many<10>(children.data());
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t depth_max> task<void> skynet() {
  size_t count = co_await skynet_one<depth_max>(0, 0);
  if (count != 499999500000) {
    std::printf("%" PRIu64 "\n", count);
  }
  done.store(true);
}
} // namespace bulk
} // namespace coro
} // namespace skynet

constexpr size_t NRUNS = 100;
tmc::task<int> bench_main() {
  for (size_t i = 0; i < NRUNS; ++i) {
    co_await skynet::coro::bulk::skynet<6>();
  }
  co_return 0;
}

tmc::task<void> bench_client_main_awaiter(
  tmc::task<int> client_main, std::atomic<int>* exit_code_out
) {
  client_main.resume_on(tmc::cpu_executor());
  int exit_code = co_await client_main;
  exit_code_out->store(exit_code);
  exit_code_out->notify_all();
}
int bench_async_main(tmc::task<int> client_main) {
  std::atomic<int> exit_code(std::numeric_limits<int>::min());

  // these time points are matched to the original bench locations
  auto start_time = std::chrono::high_resolution_clock::now();

  tmc::cpu_executor().set_thread_count(NTHREADS).init();
  post(
    tmc::cpu_executor(), bench_client_main_awaiter(client_main, &exit_code), 0
  );
  exit_code.wait(std::numeric_limits<int>::min());

  auto end_time = std::chrono::high_resolution_clock::now();
  auto total_time_ns = std::chrono::duration_cast<std::chrono::microseconds>(
    end_time - start_time
  );
  std::printf("%" PRIu64 "", total_time_ns.count() / NRUNS);

  tmc::cpu_executor().teardown();
  return exit_code.load();
}
int main() { return bench_async_main(bench_main()); }

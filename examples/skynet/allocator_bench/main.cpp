#define TMC_IMPL
#include "build/bench_config.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/task.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>

using namespace tmc;

namespace skynet {
namespace coro {
namespace bulk {
static std::atomic_bool done;
// all tasks are spawned at the same priority
template <size_t DepthMax>
task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
  if (Depth == DepthMax) {
    co_return BaseNum;
  }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }
  std::array<task<size_t>, 10> children;
  for (size_t idx = 0; idx < 10; ++idx) {
    children[idx] =
      skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
  }
  std::array<size_t, 10> results = co_await spawn_many<10>(children.data());
  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t DepthMax> task<void> skynet() {
  size_t count = co_await skynet_one<DepthMax>(0, 0);
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
  tmc::task<int> ClientMainTask, std::atomic<int>* ExitCode_out
) {
  ClientMainTask.resume_on(tmc::cpu_executor());
  int exit_code = co_await ClientMainTask;
  ExitCode_out->store(exit_code);
  ExitCode_out->notify_all();
}
int bench_async_main(tmc::task<int> ClientMainTask) {
  std::atomic<int> exitCode(std::numeric_limits<int>::min());

  // these time points are matched to the original bench locations
  auto startTime = std::chrono::high_resolution_clock::now();

  tmc::cpu_executor().set_thread_count(NTHREADS).init();
  post(
    tmc::cpu_executor(), bench_client_main_awaiter(ClientMainTask, &exitCode), 0
  );
  exitCode.wait(std::numeric_limits<int>::min());

  auto endTime = std::chrono::high_resolution_clock::now();
  auto totalTimeNs =
    std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
  std::printf("%" PRIu64 "", totalTimeNs.count() / NRUNS);

  tmc::cpu_executor().teardown();
  return exitCode.load();
}
int main() { return bench_async_main(bench_main()); }

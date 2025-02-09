// Run the same number of small tasks with increasing numbers of threads,
// from 1 to 32 threads. Find the point of diminishing returns.

#define TMC_IMPL

#include "tmc/ex_cpu.hpp"
#include "tmc/sync.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <future>
#include <ranges>

using namespace tmc;

#define USE_ITERATOR
constexpr int WARMUP_COUNT = 10;

struct bench_result {
  size_t thread_count;
  size_t task_count;
  std::chrono::duration<long, std::ratio<1, 1000000000>> post_dur_ns;
  std::chrono::duration<long, std::ratio<1, 1000000000>> dur_ns;
};

[[maybe_unused]] static task<void> make_task(size_t& DataSlot) {
  int a = 0;
  int b = 1;
#pragma unroll 1
  for (int i = 0; i < 50; ++i) {
#pragma unroll 1
    for (int j = 0; j < 25; ++j) {
      a = a + b;
      b = b + a;
    }
  }

  DataSlot = b;
  co_return;
}

static bench_result find_equilibrium(size_t Count, size_t ThreadCount) {
  auto& executor = tmc::cpu_executor();
  executor.set_thread_count(ThreadCount).init();
  std::vector<size_t> data;
  data.resize(Count);
  std::future<void> future;
#ifdef USE_ITERATOR
  for (size_t i = 0; i < WARMUP_COUNT; ++i) {
    future = post_bulk_waitable(
      executor, std::ranges::views::transform(data, make_task).begin(), Count, 0
    );
    future.wait();
  }
  auto beforePostTime = std::chrono::high_resolution_clock::now();
  future = post_bulk_waitable(
    executor, std::ranges::views::transform(data, make_task).begin(), Count, 0
  );
#else
  std::vector<task<void>> tasks;
  tasks.resize(Count);
  for (size_t i = 0; i < WARMUP_COUNT; ++i) {
    for (size_t taskidx = 0; taskidx < Count; ++taskidx) {
      tasks[taskidx] = make_task(data[taskidx]);
    }
    future = post_bulk_waitable(executor, tasks.begin(), Count, 0);
    future.wait();
  }
  auto beforePostTime = std::chrono::high_resolution_clock::now();
  for (size_t taskidx = 0; taskidx < Count; ++taskidx) {
    tasks[taskidx] = make_task(data[taskidx]);
  }
  future = post_bulk_waitable(executor, tasks.begin(), Count, 0);
#endif

  auto afterPostTime = std::chrono::high_resolution_clock::now();
  future.wait();
  auto doneTime = std::chrono::high_resolution_clock::now();

  auto postDur = std::chrono::duration_cast<std::chrono::nanoseconds>(
    afterPostTime - beforePostTime
  );
  auto totalDur = std::chrono::duration_cast<std::chrono::nanoseconds>(
    doneTime - beforePostTime
  );

  tmc::cpu_executor().teardown();
  return bench_result{ThreadCount, Count, postDur, totalDur};
}

int main() {
  size_t count = 64000;
  std::array<bench_result, 32> results;
  for (size_t i = 0; i < results.size(); ++i) {
    results[i] = find_equilibrium(count, i + 1);
  }

  std::printf("%zu tasks\n", count);
  for (size_t i = 0; i < results.size(); ++i) {
    auto benchResult = results[i];
    std::printf(
      "%zu thr, %ld post ns, %ld tot ns: %zu ns/task (wall), %zu "
      "thread-ns/task\n",
      benchResult.thread_count, benchResult.post_dur_ns.count(),
      benchResult.dur_ns.count(), benchResult.dur_ns.count() / count,
      (benchResult.thread_count) * benchResult.dur_ns.count() / count
    );
  }
  //}
}

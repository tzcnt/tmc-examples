// Run the same number of small tasks with increasing numbers of threads,
// from 1 to 32 threads. Find the point of diminishing returns.

#define TMC_IMPL
#include "tmc/all_headers.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>

using namespace tmc;

#define USE_TRANSFORMER
constexpr int WARMUP_COUNT = 10;

struct bench_result {
  size_t thread_count;
  size_t task_count;
  std::chrono::duration<long, std::ratio<1, 1000000000>> post_dur_ns;
  std::chrono::duration<long, std::ratio<1, 1000000000>> dur_ns;
};

task<void> make_task(uint64_t* DataSlot) {
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

  *DataSlot = b;
  co_return;
}

task<void> get_task(size_t Slot, uint64_t* Data) {
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

  Data[Slot] = b;
  co_return;
}

bench_result find_equilibrium(size_t Count, size_t ThreadCount) {
  auto& executor = tmc::cpu_executor();
  executor.set_thread_count(ThreadCount).init();
  auto data = new uint64_t[Count];
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::future<void> future;
#ifdef USE_TRANSFORMER
  // this is around 100ns slower per-task :(
  for (size_t i = 0; i < WARMUP_COUNT; ++i) {
    future =
      post_bulk_waitable(executor, iter_adapter(data, make_task), 0, Count);
    future.wait();
  }
  auto beforePostTime = std::chrono::high_resolution_clock::now();
  future =
    post_bulk_waitable(executor, iter_adapter(data, make_task), 0, Count);
#else
  auto tasks = new task<void>[Count];
  for (size_t i = 0; i < WARMUP_COUNT; ++i) {
    for (size_t taskidx = 0; taskidx < Count; ++taskidx) {
      tasks[taskidx] = get_task(taskidx, data);
    }
    future = post_bulk_waitable(executor, tasks, 0, Count);
    future.wait();
  }
  auto beforePostTime = std::chrono::high_resolution_clock::now();
  for (size_t taskidx = 0; taskidx < Count; ++taskidx) {
    tasks[taskidx] = get_task(taskidx, data);
  }
  future = post_bulk_waitable(executor, tasks, 0, Count);
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
  delete[] data;
#ifndef USE_TRANSFORMER
  delete[] tasks;
#endif
  return bench_result{ThreadCount, Count, postDur, totalDur};
}

int main() {
  size_t count = 64000;
  std::array<bench_result, 32> results;
  for (size_t i = 0; i < results.size(); ++i) {
    results[i] = find_equilibrium(count, i + 1);
  }

  std::printf("%" PRIu64 " tasks\n", count);
  for (size_t i = 0; i < results.size(); ++i) {
    auto benchResult = results[i];
    std::printf(
      "%" PRIu64 " thr, %ld post ns, %ld tot ns: %" PRIu64
      " ns/task (wall), %" PRIu64 " "
      "thread-ns/task\n",
      benchResult.thread_count, benchResult.post_dur_ns.count(),
      benchResult.dur_ns.count(), benchResult.dur_ns.count() / count,
      (benchResult.thread_count) * benchResult.dur_ns.count() / count
    );
  }
  //}
}

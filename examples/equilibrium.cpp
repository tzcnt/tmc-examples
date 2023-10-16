// Run the same number of small tasks with increasing numbers of threads,
// from 1 to 32 threads. Find the point of diminishing returns.

#define TMC_IMPL
#include "tmc/all_headers.hpp"
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <coroutine>
#include <iostream>
#include <thread>
using namespace std::chrono;
using namespace std::chrono_literals;
using namespace tmc;
#define USE_TRANSFORMER
constexpr int WARMUP_COUNT = 10;

struct bench_result {
  size_t thread_count;
  size_t task_count;
  std::chrono::duration<long, std::ratio<1, 1000000000>> post_dur_ns;
  std::chrono::duration<long, std::ratio<1, 1000000000>> dur_ns;
};

task<void> make_task(uint64_t* out_ptr) {
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

  *out_ptr = b;
  co_return;
}

task<void> get_task(size_t slot, uint64_t* data) {
  auto slot_in = slot;
  auto& data_in = data;
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

  data[slot] = b;
  co_return;
}

bench_result find_equilibrium(size_t count, size_t nthreads) {
  auto& executor = tmc::cpu_executor();
  executor.set_thread_count(nthreads).init();
  auto data = new uint64_t[count];
  for (size_t i = 0; i < count; ++i) {
    data[i] = 0;
  }
  std::future<void> future;
#ifdef USE_TRANSFORMER
  // this is around 100ns slower per-task :(
  for (size_t i = 0; i < WARMUP_COUNT; ++i) {
    future =
      post_bulk_waitable(executor, iter_adapter(data, make_task), 0, count);
    future.wait();
  }
  auto pre = std::chrono::high_resolution_clock::now();
  future =
    post_bulk_waitable(executor, iter_adapter(data, make_task), 0, count);
#else
  auto tasks = new task<void>[count];
  for (size_t i = 0; i < WARMUP_COUNT; ++i) {
    for (size_t taskidx = 0; taskidx < count; ++taskidx) {
      tasks[taskidx] = get_task(taskidx, data);
    }
    future = post_bulk_waitable(executor, tasks, 0, count);
    future.wait();
  }
  auto pre = std::chrono::high_resolution_clock::now();
  for (size_t taskidx = 0; taskidx < count; ++taskidx) {
    tasks[taskidx] = get_task(taskidx, data);
  }
  future = post_bulk_waitable(executor, tasks, 0, count);
#endif

  auto post_done = std::chrono::high_resolution_clock::now();
  future.wait();
  auto done = std::chrono::high_resolution_clock::now();

  auto post_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(post_done - pre);
  auto total_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);

  tmc::cpu_executor().teardown();
  delete[] data;
#ifndef USE_TRANSFORMER
  delete[] tasks;
#endif
  return bench_result{nthreads, count, post_dur, total_dur};
}

int main() {
  size_t count = 64000;
  std::array<bench_result, 32> results;
  for (size_t i = 0; i < results.size(); ++i) {
    results[i] = find_equilibrium(count, i + 1);
  }

  std::printf("%" PRIu64 " tasks\n", count);
  for (size_t i = 0; i < results.size(); ++i) {
    auto bench_result = results[i];
    std::printf(
      "%" PRIu64 " thr, %ld post ns, %ld tot ns: %" PRIu64
      " ns/task (wall), %" PRIu64 " "
      "thread-ns/task\n",
      bench_result.thread_count, bench_result.post_dur_ns.count(),
      bench_result.dur_ns.count(), bench_result.dur_ns.count() / count,
      (bench_result.thread_count) * bench_result.dur_ns.count() / count
    );
  }
  //}
}

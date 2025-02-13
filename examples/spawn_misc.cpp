// Miscellaneous ways to spawn and await tasks

#define TMC_IMPL

#include "tmc/aw_yield.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/sync.hpp"

#include <chrono>
#include <cstdio>
#include <future>
#include <ranges>

using namespace tmc;

template <size_t Count, size_t ThreadCount> void small_func_spawn_bench_lazy() {
  std::printf("small_func_spawn_bench_lazy()...\n");
  ex_cpu executor;
  executor.set_thread_count(ThreadCount).init();
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto preTime = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < Count; ++i) {
    // because this is a functor and not a coroutine,
    // it is OK to capture the loop variables
    results[i] = post_waitable(executor, [i, &data]() { data[i] = i; }, 0);
  }
  auto postTime = std::chrono::high_resolution_clock::now();
  for (auto& f : results) {
    f.wait();
  }
  auto doneTime = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < Count; ++i) {
    if (data[i] != i) {
      std::printf("FAIL: index %zu value %zu", i, data[i]);
    }
  }

  size_t spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime)
      .count();
  std::printf(
    "spawned %zu tasks in %zu ns: %zu ns/task\n", Count, spawnDur,
    spawnDur / Count
  );

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime)
      .count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count, ThreadCount * (execDur / Count)
  );
}

template <size_t Count, size_t nthreads> void large_task_spawn_bench_lazy() {
  std::printf("large_task_spawn_bench_lazy()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto preTime = std::chrono::high_resolution_clock::now();
  for (size_t slot = 0; slot < Count; ++slot) {
    // because this is a coroutine and not a functor, it is not safe to capture
    // https://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines/avoid-capturing-lambda-coroutines.html
    // variables must be passed as parameters instead
    results[slot] = post_waitable(
      executor,
      [](size_t* DataSlot) -> task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 1000; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
          co_await yield_if_requested();
        }
        *DataSlot = b;
      }(&data[slot]),
      0
    );
  }
  auto postTime = std::chrono::high_resolution_clock::now();
  for (auto& f : results) {
    f.wait();
  }
  auto doneTime = std::chrono::high_resolution_clock::now();

  size_t spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime)
      .count();
  std::printf(
    "spawned %zu tasks in %zu ns: %zu ns/task\n", Count, spawnDur,
    spawnDur / Count
  );

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime)
      .count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count, nthreads * execDur / Count
  );
}

template <size_t Count, size_t nthreads>
void large_task_spawn_bench_lazy_bulk() {
  std::printf("large_task_spawn_bench_lazy_bulk()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  auto preTime = std::chrono::high_resolution_clock::now();
  auto tasks =
    std::ranges::views::transform(data, [](size_t& DataSlot) -> task<void> {
      int a = 0;
      int b = 1;
      for (int i = 0; i < 1000; ++i) {
        for (int j = 0; j < 500; ++j) {
          a = a + b;
          b = b + a;
        }
        co_await yield_if_requested();
      }
      DataSlot = b;
    });
  auto future = post_bulk_waitable(executor, tasks.begin(), Count, 0);
  auto postTime = std::chrono::high_resolution_clock::now();
  future.wait();
  auto doneTime = std::chrono::high_resolution_clock::now();

  size_t spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime)
      .count();
  std::printf(
    "spawned %zu tasks in %zu ns: %zu ns/task\n", Count, spawnDur,
    spawnDur / Count
  );

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime)
      .count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count, nthreads * execDur / Count
  );
}

// Dispatch lowest prio -> highest prio so that each task is interrupted
template <size_t Count, size_t nthreads, size_t npriorities>
void prio_reversal_test() {
  std::printf("prio_reversal_test()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).set_priority_count(npriorities).init();
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto preTime = std::chrono::high_resolution_clock::now();
  size_t slot = 0;
  while (true) {
    for (size_t prio = npriorities - 1; prio != static_cast<size_t>(-1);
         --prio) {
      results[slot] = post_waitable(
        executor,
        [](size_t* DataSlot, [[maybe_unused]] size_t Priority) -> task<void> {
          int a = 0;
          int b = 1;
          for (int i = 0; i < 1000; ++i) {
            for (int j = 0; j < 500; ++j) {
              a = a + b;
              b = b + a;
            }
            if (yield_requested()) {
              co_await yield();
            }
          }

          *DataSlot = b;
        }(&data[slot], prio),
        prio
      );
      slot++;
      if (slot == Count) {
        goto DONE;
      }
      // uncomment this for small number of nthreads
      // std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  }
DONE:

  auto postTime = std::chrono::high_resolution_clock::now();
  for (auto& f : results) {
    f.wait();
  }
  auto doneTime = std::chrono::high_resolution_clock::now();

  size_t spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime)
      .count();
  std::printf(
    "spawned %zu tasks in %zu ns: %zu ns/task\n", Count, spawnDur,
    spawnDur / Count
  );

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime)
      .count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count, nthreads * execDur / Count
  );
}

int main() {
  small_func_spawn_bench_lazy<100, 16>();
  large_task_spawn_bench_lazy<100, 16>();
  large_task_spawn_bench_lazy_bulk<100, 16>();
  prio_reversal_test<100, 16, 63>();
}

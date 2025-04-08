// A demonstration of the capabilities and performance of `ex_braid`
// It is both a serializing executor, and an async mutex

#define TMC_IMPL

#include "tmc/ex_braid.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/spawn_task.hpp"
#include "tmc/task.hpp"

#include <chrono>
#include <cstdio>
#include <ranges>
#include <vector>

template <size_t Count> tmc::task<void> large_task_spawn_bench_lazy_bulk() {
  tmc::ex_braid br;
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  auto pre = std::chrono::high_resolution_clock::now();
  auto tasks =
    std::ranges::views::transform(data, [](size_t& elem) -> tmc::task<void> {
      return [](size_t* Elem) -> tmc::task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 1000; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        *Elem = b;
        co_return;
      }(&elem);
    });
  co_await tmc::spawn_many<Count>(tasks.begin()).run_on(br);
  auto done = std::chrono::high_resolution_clock::now();

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre).count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count,
    tmc::cpu_executor().thread_count() * execDur / Count
  );
}

template <size_t Count> tmc::task<void> braid_lock() {
  tmc::ex_braid br;
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  size_t value = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  auto tasks = std::ranges::views::transform(
    data,
    [&br, &value](size_t& elem) -> tmc::task<void> {
      return [](
               size_t* Elem, tmc::ex_braid* Braid, size_t* Value
             ) -> tmc::task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 1000; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        *Elem = b;
        co_await tmc::enter(Braid);
        *Value = *Value + b;
        // not necessary to exit the braid scope, since the task has ended
      }(&elem, &br, &value);
    }
  );
  co_await tmc::spawn_many<Count>(tasks.begin());
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * Count) {
    std::printf("FAIL: expected %zu but got %zu\n", data[0] * Count, value);
  }
  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre).count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count,
    tmc::cpu_executor().thread_count() * execDur / Count
  );
}

template <size_t Count> tmc::task<void> braid_lock_middle() {
  tmc::ex_braid br;
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  size_t value = 0;
  size_t lockCount = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  std::vector<tmc::task<void>> tasks;
  tasks.resize(Count);
  {
    for (size_t slot = 0; slot < Count; ++slot) {
      tasks[slot] = [](
                      auto* DataSlot, tmc::ex_braid* Braid, size_t* Value,
                      size_t* LockCount
                    ) -> tmc::task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 499; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        a = a + b;
        b = b + a;
        auto braidScope = co_await tmc::enter(Braid);
        (*LockCount)++;
        co_await braidScope.exit();
        for (int i = 0; i < 500; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }

        *DataSlot = b;
        co_await tmc::enter(Braid);
        *Value = *Value + b;
        // not necessary to exit the braid scope, since the task has ended
      }(&data[slot], &br, &value, &lockCount);
    }
  }
  co_await spawn_many(tasks.data(), Count);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * Count) {
    std::printf("FAIL: expected %zu but got %zu\n", data[0] * Count, value);
  }
  if (lockCount != Count) {
    std::printf("FAIL: expected %zu but got %zu\n", data[0] * Count, value);
  }

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre).count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count,
    tmc::cpu_executor().thread_count() * execDur / Count
  );
}

template <size_t Count> tmc::task<void> braid_lock_middle_resume_on() {
  tmc::ex_braid br;
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  size_t value = 0;
  size_t lockCount = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  std::vector<tmc::task<void>> tasks;
  tasks.resize(Count);
  {
    for (size_t slot = 0; slot < Count; ++slot) {
      tasks[slot] = [](
                      auto* DataSlot, tmc::ex_braid* Braid, size_t* Value,
                      size_t* LockCount
                    ) -> tmc::task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 499; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        a = a + b;
        b = b + a;
        co_await resume_on(Braid);
        (*LockCount)++;
        co_await resume_on(tmc::cpu_executor());
        for (int i = 0; i < 500; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }

        *DataSlot = b;
        co_await resume_on(Braid);
        *Value = *Value + b;
      }(&data[slot], &br, &value, &lockCount);
    }
  }
  co_await spawn_many(tasks.data(), Count);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * Count) {
    std::printf("FAIL: expected %zu but got %zu\n", data[0] * Count, value);
  }
  if (lockCount != Count) {
    std::printf("FAIL: expected %zu but got %zu\n", data[0] * Count, value);
  }
  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre).count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count,
    tmc::cpu_executor().thread_count() * execDur / Count
  );
}

tmc::task<void> increment(size_t* Value) {
  (*Value)++;
  co_return;
}

template <size_t Count> tmc::task<void> braid_lock_middle_child_task() {
  tmc::ex_braid br;
  std::array<size_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  size_t value = 0;
  size_t lockCount = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  std::vector<tmc::task<void>> tasks;
  tasks.resize(Count);
  {
    for (size_t slot = 0; slot < Count; ++slot) {
      tasks[slot] = [](
                      auto* DataSlot, tmc::ex_braid* Braid, size_t* Value,
                      size_t* LockCount
                    ) -> tmc::task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 499; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        a = a + b;
        b = b + a;
        co_await tmc::spawn(increment(LockCount)).run_on(Braid);
        for (int i = 0; i < 500; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }

        *DataSlot = b;
        co_await tmc::enter(Braid);
        *Value = *Value + b;
        // not necessary to exit the braid scope, since the task has ended
      }(&data[slot], &br, &value, &lockCount);
    }
  }
  co_await spawn_many(tasks.data(), Count);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * Count) {
    std::printf("FAIL: expected %zu but got %zu\n", data[0] * Count, value);
  }
  if (lockCount != Count) {
    std::printf("FAIL: expected %zu but got %zu\n", data[0] * Count, value);
  }

  size_t execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre).count();
  std::printf(
    "executed %zu tasks in %zu ns: %zu ns/task (wall), %zu "
    "ns/task/thread\n",
    Count, execDur, execDur / Count,
    tmc::cpu_executor().thread_count() * execDur / Count
  );
}
static tmc::task<int> async_main() {
  co_await large_task_spawn_bench_lazy_bulk<1000>();
  co_await braid_lock<32000>();
  co_await braid_lock_middle<32000>();
  co_await braid_lock_middle_resume_on<32000>();
  co_await braid_lock_middle_child_task<32000>();
  co_return 0;
  // TODO clean these up - they don't need all the a + b stuff...
}
int main() { return tmc::async_main(async_main()); }

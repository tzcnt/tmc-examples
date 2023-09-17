#define TMC_IMPL
#include "tmc/aw_resume_on.hpp"
#include "tmc/ex_braid.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include <vector>
using namespace tmc;
template <size_t COUNT> tmc::task<void> large_task_spawn_bench_lazy_bulk() {
  ex_braid br;
  // TODO make spawn_many take an executor parameter so we don't need to
  // preemptively enter() here
  co_await br.enter();
  std::array<uint64_t, COUNT> data;
  for (size_t i = 0; i < COUNT; ++i) {
    data[i] = 0;
  }
  auto pre = std::chrono::high_resolution_clock::now();
  co_await spawn_many(iter_adapter(data.data(),
                                   [](auto *data_ptr) -> task<void> {
                                     int a = 0;
                                     int b = 1;
                                     for (int i = 0; i < 1000; ++i) {
                                       for (int j = 0; j < 500; ++j) {
                                         a = a + b;
                                         b = b + a;
                                       }
                                     }
                                     *data_ptr = b;
                                     co_return;
                                   }),
                      0, COUNT);
  auto done = std::chrono::high_resolution_clock::now();

  auto exec_dur =
      std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
      "executed %ld tasks in %ld ns: %ld ns/task (wall), %ld ns/task/thread\n",
      COUNT, exec_dur.count(), exec_dur.count() / COUNT,
      (16) * exec_dur.count() / COUNT);
}

template <size_t COUNT> tmc::task<void> braid_lock() {
  ex_braid br;
  std::array<uint64_t, COUNT> data;
  for (size_t i = 0; i < COUNT; ++i) {
    data[i] = 0;
  }
  uint64_t value = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  // std::vector<std::coroutine_handle<>> tasks;
  // tasks.resize(COUNT);
  // for (uint64_t slot = 0; slot < COUNT; ++slot) {
  //   tasks[slot] = [](auto *data_ptr, ex_braid *braid_lock, uint64_t
  //   *value_ptr)
  //   -> task<void> {
  //     int a = 0;
  //     int b = 1;
  //     for (int i = 0; i < 1000; ++i) {
  //       for (int j = 0; j < 500; ++j) {
  //         a = a + b;
  //         b = b + a;
  //       }
  //     }
  //
  //     *data_ptr = b;
  //     co_await braid_lock->enter();
  //     *value_ptr = *value_ptr + b;
  //     // for example, but not necessary since the task ends here
  //     // co_await braid_lock->exit();
  //   }(&data[slot], &s, &value);
  // }
  // executor.post_bulk(tasks.data(), 0, COUNT);
  co_await spawn_many(
      iter_adapter(data.data(),
                   [&br, &value](auto *data_ptr) -> task<void> {
                     return [](auto *data_ptr, ex_braid *braid_lock,
                               uint64_t *value_ptr) -> task<void> {
                       int a = 0;
                       int b = 1;
                       for (int i = 0; i < 1000; ++i) {
                         for (int j = 0; j < 500; ++j) {
                           a = a + b;
                           b = b + a;
                         }
                       }

                       *data_ptr = b;
                       co_await braid_lock->enter();
                       *value_ptr = *value_ptr + b;
                       //  for example, but not necessary since the task ends
                       //  here co_await braid_lock->exit();
                     }(data_ptr, &br, &value);
                   }),
      0, COUNT);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * COUNT) {
    std::printf("FAIL: expected %ld but got %ld\n", data[0] * COUNT, value);
  }
  auto exec_dur =
      std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
      "executed %ld tasks in %ld ns: %ld ns/task (wall), %ld ns/task/thread\n",
      COUNT, exec_dur.count(), exec_dur.count() / COUNT,
      (16) * exec_dur.count() / COUNT);
}

template <size_t COUNT> tmc::task<void> braid_lock_middle() {
  ex_braid br;
  std::array<uint64_t, COUNT> data;
  for (size_t i = 0; i < COUNT; ++i) {
    data[i] = 0;
  }
  uint64_t value = 0;
  uint64_t mid_locks = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  std::vector<tmc::task<void>> tasks;
  tasks.resize(COUNT);
  {
    for (uint64_t slot = 0; slot < COUNT; ++slot) {
      tasks[slot] = [](auto *data_ptr, ex_braid *braid_lock,
                       uint64_t *value_ptr,
                       uint64_t *lock_count_ptr) -> task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 499; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        co_await braid_lock->enter();
        a = a + b;
        b = b + a;
        (*lock_count_ptr)++;
        co_await braid_lock->exit();
        for (int i = 0; i < 500; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }

        *data_ptr = b;
        co_await braid_lock->enter();
        *value_ptr = *value_ptr + b;
        // for example, but not necessary since the task ends here
        // co_await braid_lock->exit();
      }(&data[slot], &br, &value, &mid_locks);
    }
  }
  co_await spawn_many(tasks.data(), 0, COUNT);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * COUNT) {
    std::printf("FAIL: expected %ld but got %ld\n", data[0] * COUNT, value);
  }
  if (mid_locks != COUNT) {
    std::printf("FAIL: expected %ld but got %ld\n", data[0] * COUNT, value);
  }

  auto exec_dur =
      std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
      "executed %ld tasks in %ld ns: %ld ns/task (wall), %ld ns/task/thread\n",
      COUNT, exec_dur.count(), exec_dur.count() / COUNT,
      (16) * exec_dur.count() / COUNT);
}

template <size_t COUNT> tmc::task<void> braid_lock_middle_resume_on() {
  ex_braid br;
  std::array<uint64_t, COUNT> data;
  for (size_t i = 0; i < COUNT; ++i) {
    data[i] = 0;
  }
  uint64_t value = 0;
  uint64_t mid_locks = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  std::vector<tmc::task<void>> tasks;
  tasks.resize(COUNT);
  {
    for (uint64_t slot = 0; slot < COUNT; ++slot) {
      tasks[slot] = [](auto *data_ptr, ex_braid *braid_lock,
                       uint64_t *value_ptr,
                       uint64_t *lock_count_ptr) -> task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 499; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        co_await resume_on(braid_lock);
        a = a + b;
        b = b + a;
        (*lock_count_ptr)++;
        co_await resume_on(tmc::cpu_executor());
        for (int i = 0; i < 500; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }

        *data_ptr = b;
        co_await resume_on(braid_lock);
        *value_ptr = *value_ptr + b;
      }(&data[slot], &br, &value, &mid_locks);
    }
  }
  co_await spawn_many(tasks.data(), 0, COUNT);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * COUNT) {
    std::printf("FAIL: expected %ld but got %ld\n", data[0] * COUNT, value);
  }
  if (mid_locks != COUNT) {
    std::printf("FAIL: expected %ld but got %ld\n", data[0] * COUNT, value);
  }
  auto exec_dur =
      std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
      "executed %ld tasks in %ld ns: %ld ns/task (wall), %ld ns/task/thread\n",
      COUNT, exec_dur.count(), exec_dur.count() / COUNT,
      (16) * exec_dur.count() / COUNT);
}

tmc::task<int> async_main() {
  co_await large_task_spawn_bench_lazy_bulk<32000>();
  co_await braid_lock<32000>();
  co_await braid_lock_middle<32000>();
  co_await braid_lock_middle_resume_on<32000>();
  co_return 0;
}
int main() { return tmc::async_main(async_main()); }

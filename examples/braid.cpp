// A demonstration of the capabilities and performance of `ex_braid`
// It is both a serializing executor, and an async mutex

#define TMC_IMPL
#include "tmc/all_headers.hpp"

#include <cinttypes>
#include <cstdio>
#include <vector>

using namespace tmc;

template <size_t Count> tmc::task<void> large_task_spawn_bench_lazy_bulk() {
  ex_braid br;
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  auto pre = std::chrono::high_resolution_clock::now();
  co_await spawn_many(
    iter_adapter(
      data.data(),
      [](auto* Data) -> task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 1000; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }
        *Data = b;
        co_return;
      }
    ),
    Count
  )
    .run_on(br);
  auto done = std::chrono::high_resolution_clock::now();

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    (16) * exec_dur.count() / Count
  );
}

template <size_t Count> tmc::task<void> braid_lock() {
  ex_braid br;
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  uint64_t value = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  co_await spawn_many(
    iter_adapter(
      data.data(),
      [&br, &value](auto* Data) -> task<void> {
        return [](
                 auto* task_data, ex_braid* braid_lock, uint64_t* value_ptr
               ) -> task<void> {
          int a = 0;
          int b = 1;
          for (int i = 0; i < 1000; ++i) {
            for (int j = 0; j < 500; ++j) {
              a = a + b;
              b = b + a;
            }
          }

          *task_data = b;
          co_await tmc::enter(braid_lock);
          *value_ptr = *value_ptr + b;
          //  for example, but not necessary since the task ends
          //  here co_await braid_lock->exit();
        }(Data, &br, &value);
      }
    ),
    Count
  );
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * Count) {
    std::printf(
      "FAIL: expected %" PRIu64 " but got %" PRIu64 "\n", data[0] * Count, value
    );
  }
  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    (16) * exec_dur.count() / Count
  );
}

template <size_t Count> tmc::task<void> braid_lock_middle() {
  ex_braid br;
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  uint64_t value = 0;
  uint64_t mid_locks = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  std::vector<tmc::task<void>> tasks;
  tasks.resize(Count);
  {
    for (uint64_t slot = 0; slot < Count; ++slot) {
      tasks[slot] = [](
                      auto* Data, ex_braid* braid_lock, uint64_t* value_ptr,
                      uint64_t* lock_count_ptr
                    ) -> task<void> {
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
        auto braid_scope = co_await tmc::enter(braid_lock);
        (*lock_count_ptr)++;
        co_await braid_scope.exit();
        for (int i = 0; i < 500; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }

        *Data = b;
        co_await tmc::enter(braid_lock);
        *value_ptr = *value_ptr + b;
        // for example, but not necessary since the task ends here
        // co_await braid_lock->exit();
      }(&data[slot], &br, &value, &mid_locks);
    }
  }
  co_await spawn_many(tasks.data(), Count);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * Count) {
    std::printf(
      "FAIL: expected %" PRIu64 " but got %" PRIu64 "\n", data[0] * Count, value
    );
  }
  if (mid_locks != Count) {
    std::printf(
      "FAIL: expected %" PRIu64 " but got %" PRIu64 "\n", data[0] * Count, value
    );
  }

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    (16) * exec_dur.count() / Count
  );
}

template <size_t Count> tmc::task<void> braid_lock_middle_resume_on() {
  ex_braid br;
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  uint64_t value = 0;
  uint64_t mid_locks = 0;
  auto pre = std::chrono::high_resolution_clock::now();
  std::vector<tmc::task<void>> tasks;
  tasks.resize(Count);
  {
    for (uint64_t slot = 0; slot < Count; ++slot) {
      tasks[slot] = [](
                      auto* Data, ex_braid* braid_lock, uint64_t* value_ptr,
                      uint64_t* lock_count_ptr
                    ) -> task<void> {
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
        co_await resume_on(braid_lock);
        (*lock_count_ptr)++;
        co_await resume_on(tmc::cpu_executor());
        for (int i = 0; i < 500; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
        }

        *Data = b;
        co_await resume_on(braid_lock);
        *value_ptr = *value_ptr + b;
      }(&data[slot], &br, &value, &mid_locks);
    }
  }
  co_await spawn_many(tasks.data(), Count);
  auto done = std::chrono::high_resolution_clock::now();

  if (value != data[0] * Count) {
    std::printf(
      "FAIL: expected %" PRIu64 " but got %" PRIu64 "\n", data[0] * Count, value
    );
  }
  if (mid_locks != Count) {
    std::printf(
      "FAIL: expected %" PRIu64 " but got %" PRIu64 "\n", data[0] * Count, value
    );
  }
  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - pre);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    (16) * exec_dur.count() / Count
  );
}

tmc::task<int> async_main() {
  co_await large_task_spawn_bench_lazy_bulk<32000>();
  co_await braid_lock<32000>();
  co_await braid_lock_middle<32000>();
  co_await braid_lock_middle_resume_on<32000>();
  co_return 0;
}
int main() { return tmc::async_main(async_main()); }

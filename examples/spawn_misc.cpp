// Miscellaneous ways to spawn and await tasks

#define TMC_IMPL
#include "tmc/all_headers.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>

using namespace tmc;

template <size_t Count, size_t ThreadCount> void small_task_spawn_bench_lazy() {
  ex_cpu executor;
  executor.set_thread_count(ThreadCount).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto pre = std::chrono::high_resolution_clock::now();
  for (uint64_t i = 0; i < Count; ++i) {
    results[i] = post_waitable(
      executor,
      [i, &data]() {
        auto i_param = i;
        auto& data_param = data;
        data_param[i_param] = i_param;
      },
      0
    );
  }
  auto post = std::chrono::high_resolution_clock::now();
  for (auto& f : results) {
    f.wait();
  }
  auto done = std::chrono::high_resolution_clock::now();

  for (uint64_t i = 0; i < Count; ++i) {
    if (data[i] != i) {
      std::printf("FAIL: index %" PRIu64 " value %" PRIu64 "", i, data[i]);
    }
  }

  auto spawn_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(post - pre);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawn_dur.count(), spawn_dur.count() / Count
  );

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - post);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    ThreadCount * exec_dur.count() / Count
  );
}

template <size_t Count, size_t nthreads> void large_task_spawn_bench_lazy() {
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto pre = std::chrono::high_resolution_clock::now();
  for (uint64_t slot = 0; slot < Count; ++slot) {
    results[slot] = post_waitable(
      executor,
      // this workaround works with lazy pool + eager coro only
      [slot, &data]() -> task<void> {
        auto slot_in = slot;
        auto& data_in = data;
        int a = 0;
        int b = 1;
        for (int i = 0; i < 1000; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
          co_await yield_if_requested();
        }
        data_in[slot_in] = b;
      },
      0
    );
  }
  auto post = std::chrono::high_resolution_clock::now();
  for (auto& f : results) {
    f.wait();
  }
  auto done = std::chrono::high_resolution_clock::now();

  auto spawn_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(post - pre);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawn_dur.count(), spawn_dur.count() / Count
  );

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - post);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    nthreads * exec_dur.count() / Count
  );
}

template <size_t Count, size_t nthreads>
void large_task_spawn_bench_lazy_bulk() {
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  auto pre = std::chrono::high_resolution_clock::now();
  auto future = post_bulk_waitable(
    executor,
    iter_adapter(
      data.data(),
      [](uint64_t* Data) -> task<void> {
        int a = 0;
        int b = 1;
        for (int i = 0; i < 1000; ++i) {
          for (int j = 0; j < 500; ++j) {
            a = a + b;
            b = b + a;
          }
          co_await yield_if_requested();
        }
        *Data = b;
      }
    ),
    0, Count
  );
  auto post = std::chrono::high_resolution_clock::now();
  future.wait();
  auto done = std::chrono::high_resolution_clock::now();

  auto spawn_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(post - pre);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawn_dur.count(), spawn_dur.count() / Count
  );

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - post);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    nthreads * exec_dur.count() / Count
  );
}

// Dispatch lowest prio -> highest prio so that each task is interrupted
template <size_t Count, size_t nthreads, size_t npriorities>
void prio_reversal_test() {
  ex_cpu executor;
  executor.set_priority_count(npriorities).set_thread_count(nthreads).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto pre = std::chrono::high_resolution_clock::now();
  size_t slot = 0;
  while (true) {
    for (uint64_t prio = npriorities - 1; prio != -1ULL; --prio) {
      results[slot] = post_waitable(
        executor,
        // this workaround works with lazy pool + eager coro only
        [](size_t* Data, size_t Priority) -> task<void> {
          int a = 0;
          int b = 1;
          for (int i = 0; i < 1000; ++i) {
            for (int j = 0; j < 500; ++j) {
              a = a + b;
              b = b + a;
            }
            if (yield_requested()) {
              // std::printf("su %"PRIu64"\t", prio);
              co_await yield();
            }
          }

          *Data = b;
          // std::printf("co %"PRIu64"\t", prio);
        }(data.data() + slot, prio),
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

  auto post = std::chrono::high_resolution_clock::now();
  for (auto& f : results) {
    f.wait();
  }
  auto done = std::chrono::high_resolution_clock::now();

  auto spawn_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(post - pre);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawn_dur.count(), spawn_dur.count() / Count
  );

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - post);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    nthreads * exec_dur.count() / Count
  );
}

template <size_t Count, size_t nthreads> void co_await_eager_test() {
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  auto future = post_waitable(
    executor,
    []() -> task<void> {
      auto r1 =
        co_await spawn([]() -> task<size_t> { co_return 1; }()).run_early();
      std::printf("got %" PRIu64 "\n", r1);
      auto rt2 = spawn([]() -> task<size_t> { co_return 2; }()).run_early();
      auto r2 = co_await rt2;
      std::printf("got %" PRIu64 "\n", r2);
      // Awaiting the same task multiple times returns the same result without
      // running it again This may not be valid usage in the long term...
      auto r3 = co_await rt2;
      std::printf("got %" PRIu64 "\n", r3);
      auto r4 = co_await rt2;
      std::printf("got %" PRIu64 "\n", r4);
      co_return;
    }(),
    0
  );
  future.wait();
}

template <size_t Count, size_t nthreads> void spawn_test() {
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  // auto pre = std::chrono::high_resolution_clock::now();
  auto future = post_bulk_waitable(
    executor,
    iter_adapter(
      0,
      [](size_t slot) -> task<void> {
        std::printf("%" PRIu64 ": pre outer\n", slot);
        //  TODO make spawn take an invokable
        //  instead of constructing std::function here
        spawn(std::function([slot]() {
          std::printf("%" PRIu64 ": not co_awaited spawn\n", slot);
        }));
        co_await spawn(std::function([slot]() {
          std::printf("%" PRIu64 ": co_awaited spawn\n", slot);
        }));
        co_await spawn([](size_t Slot) -> task<void> {
          std::printf("%" PRIu64 ": pre inner\n", Slot);
          co_await yield();
          std::printf("%" PRIu64 ": resume inner 1\n", Slot);
          co_await yield();
          std::printf("%" PRIu64 ": resume inner 2\n", Slot);
          co_await yield();
          std::printf("%" PRIu64 ": post inner\n", Slot);
          co_return;
        }(slot));
        std::printf("%" PRIu64 ": post outer\n", slot);
        //  this doesn't have the desired effect - the spawn'd
        //  function returns immediately, and a 2nd co_await is
        //  required
        // TODO handle functions returning task<> specially?
        co_await co_await spawn(std::function([slot]() -> task<void> {
          return [](size_t Slot) -> task<void> {
            std::printf("%" PRIu64 ": pre inner\n", Slot);
            co_await yield();
            std::printf("%" PRIu64 ": resume inner 1\n", Slot);
            co_await yield();
            std::printf("%" PRIu64 ": resume inner 2\n", Slot);
            co_await yield();
            std::printf("%" PRIu64 ": post inner\n", Slot);
            co_return;
          }(slot);
        }));
        std::printf("%" PRIu64 ": post outer\n", slot);
        co_return;
      }
    ),
    0, Count
  );
  // auto post = std::chrono::high_resolution_clock::now();
  future.wait();
  // auto done = std::chrono::high_resolution_clock::now();

  // auto spawn_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(post
  // - pre); std::printf("spawned %"PRIu64" tasks in %"PRIu64" ns: %"PRIu64"
  // ns/task\n", Count, spawn_dur.count(), spawn_dur.count() / Count);

  // auto exec_dur = std::chrono::duration_cast<std::chrono::nanoseconds>(done -
  // post); std::printf("executed %"PRIu64" tasks in %"PRIu64" ns: %"PRIu64"
  // ns/task (wall),
  // %"PRIu64" ns/task/thread\n", Count, exec_dur.count(),
  //             exec_dur.count() / Count, nthreads * exec_dur.count() /
  //             Count);
}

template <size_t Count, size_t nthreads> void spawn_value_test() {
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  auto pre = std::chrono::high_resolution_clock::now();
  auto future = post_bulk_waitable(
    executor,
    iter_adapter(
      0,
      [](size_t slot) -> task<void> {
        return [](size_t Slot) -> task<void> {
          auto slot_start = Slot;
          //  TODO make spawn take an invokable
          //  instead of constructing std::function here
          std::printf("%" PRIu64 ": pre outer\n", Slot);
          Slot = co_await [Slot]() -> task<size_t> {
            std::printf("func 0\t");
            co_return Slot + 1;
          }();
          Slot = co_await spawn(std::function([Slot]() -> size_t {
            std::printf("func 1\t");
            return Slot + 1;
          }));
          auto t = [](size_t InnerSlot) -> task<size_t> {
            co_await yield();
            co_await yield();
            std::printf("func 2\t");
            co_return InnerSlot + 1;
          }(Slot);
          Slot = co_await spawn(t);
          //  this doesn't have the desired effect - the spawn'd
          //  function returns immediately, and a 2nd co_await is
          //  required
          // TODO handle functions returning task<> specially?
          Slot =
            co_await co_await spawn(std::function([Slot]() -> task<size_t> {
              return [](size_t InnerSlot) -> task<size_t> {
                co_await yield();
                co_await yield();
                std::printf("func 3\t");
                co_return InnerSlot + 1;
              }(Slot);
            }));
          if (Slot != slot_start + 4) {
            printf(
              "expected %" PRIu64 " but got %" PRIu64 "\n", slot_start + 4, Slot
            );
          }
          std::printf("%" PRIu64 ": post outer\n", Slot);
          co_return;
        }(slot);
      }
    ),
    0, Count
  );
  auto post = std::chrono::high_resolution_clock::now();
  future.wait();
  auto done = std::chrono::high_resolution_clock::now();

  auto spawn_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(post - pre);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawn_dur.count(), spawn_dur.count() / Count
  );

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - post);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    nthreads * exec_dur.count() / Count
  );
}

template <size_t Count, size_t nthreads> void spawn_many_test() {
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  auto pre = std::chrono::high_resolution_clock::now();
  auto future = post_bulk_waitable(
    executor,
    iter_adapter(
      0,
      [](size_t slot) -> task<void> {
        //  TODO make spawn take an invokable
        //  instead of constructing std::function
        //  here
        std::printf("%" PRIu64 ": pre outer\n", slot);
        auto t = [](size_t Slot) -> task<size_t> {
          co_await yield();
          co_await yield();
          std::printf("func %" PRIu64 "\n", Slot);
          co_return Slot + 1;
        }(slot);
        auto result = co_await spawn_many<1>(&t);
        slot = result[0];
        auto t2 = [](size_t Slot) -> task<void> {
          co_await yield();
          co_await yield();
          std::printf("func %" PRIu64 "\n", Slot);
        }(slot);
        co_await spawn_many<1>(&t2);
        slot++;
        auto t3 = [](size_t Slot) -> task<void> {
          co_await yield();
          co_await yield();
          std::printf("func %" PRIu64 "\n", Slot);
        }(slot);
        spawn_many<1>(&t3);
        std::printf("%" PRIu64 ": post outer\n", slot);
        co_return;
      }
    ),
    0, Count
  );
  auto post = std::chrono::high_resolution_clock::now();
  future.wait();
  auto done = std::chrono::high_resolution_clock::now();

  auto spawn_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(post - pre);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawn_dur.count(), spawn_dur.count() / Count
  );

  auto exec_dur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(done - post);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, exec_dur.count(), exec_dur.count() / Count,
    nthreads * exec_dur.count() / Count
  );
}
int main() {
  small_task_spawn_bench_lazy<32000, 16>();
  large_task_spawn_bench_lazy<32000, 16>();
  large_task_spawn_bench_lazy_bulk<32000, 16>();
  prio_reversal_test<32000, 16, 63>();
  co_await_eager_test<1, 16>();
  spawn_test<1, 16>();
  spawn_value_test<1, 16>();
  spawn_many_test<1, 16>();
}

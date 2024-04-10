// Miscellaneous ways to spawn and await tasks

#define TMC_IMPL

#include "tmc/aw_yield.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_func.hpp"
#include "tmc/spawn_task.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/sync.hpp"
#include "tmc/utils.hpp"

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <future>

using namespace tmc;

template <size_t Count, size_t ThreadCount> void small_func_spawn_bench_lazy() {
  std::printf("small_func_spawn_bench_lazy()...\n");
  ex_cpu executor;
  executor.set_thread_count(ThreadCount).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto preTime = std::chrono::high_resolution_clock::now();
  for (uint64_t i = 0; i < Count; ++i) {
    // because this is a functor and not a coroutine,
    // it is OK to capture the loop variables
    results[i] = post_waitable(
      executor, [i, &data]() { data[i] = i; }, 0
    );
  }
  auto postTime = std::chrono::high_resolution_clock::now();
  for (auto& f : results) {
    f.wait();
  }
  auto doneTime = std::chrono::high_resolution_clock::now();

  for (uint64_t i = 0; i < Count; ++i) {
    if (data[i] != i) {
      std::printf("FAIL: index %" PRIu64 " value %" PRIu64 "", i, data[i]);
    }
  }

  auto spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawnDur.count(), spawnDur.count() / Count
  );

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, execDur.count(), execDur.count() / Count,
    ThreadCount * execDur.count() / Count
  );
}

template <size_t Count, size_t nthreads> void large_task_spawn_bench_lazy() {
  std::printf("large_task_spawn_bench_lazy()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto preTime = std::chrono::high_resolution_clock::now();
  for (uint64_t slot = 0; slot < Count; ++slot) {
    // because this is a coroutine and not a functor, it is not safe to capture
    // https://clang.llvm.org/extra/clang-tidy/checks/cppcoreguidelines/avoid-capturing-lambda-coroutines.html
    // variables must be passed as parameters instead
    results[slot] = post_waitable(
      executor,
      [](uint64_t* DataSlot) -> task<void> {
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

  auto spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawnDur.count(), spawnDur.count() / Count
  );

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, execDur.count(), execDur.count() / Count,
    nthreads * execDur.count() / Count
  );
}

template <size_t Count, size_t nthreads>
void large_task_spawn_bench_lazy_bulk() {
  std::printf("large_task_spawn_bench_lazy_bulk()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  auto preTime = std::chrono::high_resolution_clock::now();
  auto future = post_bulk_waitable(
    executor,
    iter_adapter(
      data.data(),
      [](uint64_t* DataSlot) -> task<void> {
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
      }
    ),
    0, Count
  );
  auto postTime = std::chrono::high_resolution_clock::now();
  future.wait();
  auto doneTime = std::chrono::high_resolution_clock::now();

  auto spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawnDur.count(), spawnDur.count() / Count
  );

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, execDur.count(), execDur.count() / Count,
    nthreads * execDur.count() / Count
  );
}

// Dispatch lowest prio -> highest prio so that each task is interrupted
template <size_t Count, size_t nthreads, size_t npriorities>
void prio_reversal_test() {
  std::printf("prio_reversal_test()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).set_priority_count(npriorities).init();
  std::array<uint64_t, Count> data;
  for (size_t i = 0; i < Count; ++i) {
    data[i] = 0;
  }
  std::array<std::future<void>, Count> results;
  auto preTime = std::chrono::high_resolution_clock::now();
  size_t slot = 0;
  while (true) {
    for (uint64_t prio = npriorities - 1; prio != -1ULL; --prio) {
      results[slot] = post_waitable(
        executor,
        [](size_t* DataSlot, size_t Priority) -> task<void> {
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

          *DataSlot = b;
          // std::printf("co %"PRIu64"\t", prio);
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

  auto spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawnDur.count(), spawnDur.count() / Count
  );

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, execDur.count(), execDur.count() / Count,
    nthreads * execDur.count() / Count
  );
}

template <size_t Count, size_t nthreads> void co_await_eager_test() {
  std::printf("co_await_eager_test()...\n");
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
      // running it again. This may not be valid usage in the long term...
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
  std::printf("spawn_test()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  // auto preTime = std::chrono::high_resolution_clock::now();
  auto future = post_bulk_waitable(
    executor,
    iter_adapter(
      0,
      [](size_t slot) -> task<void> {
        std::printf("%" PRIu64 ": pre outer\n", slot);
        //  TODO make spawn take an invokable
        //  instead of constructing std::function here
        spawn(std::function([slot]() {
          std::printf("%" PRIu64 ": detached spawn\n", slot);
        })
        ).detach();
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
        // in this case, the spawned function returns immediately,
        // and a 2nd co_await is required
        co_await std::move(co_await spawn(std::function([slot]() -> task<void> {
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
        })));
        std::printf("%" PRIu64 ": post outer\n", slot);
        co_return;
      }
    ),
    0, Count
  );
  // auto postTime = std::chrono::high_resolution_clock::now();
  future.wait();
  // auto doneTime = std::chrono::high_resolution_clock::now();

  // auto spawnDur =
  //   std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime);
  // std::printf(
  //   "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n",
  //   Count, spawnDur.count(), spawnDur.count() / Count
  // );

  // auto execDur =
  //   std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime -
  //   postTime);
  // std::printf(
  //   "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
  //   " ns / task(wall), %" PRIu64 " ns / task / thread\n ",
  //   Count, execDur.count(), execDur.count() / Count,
  //   nthreads * execDur.count() / Count
  // );
}

template <size_t Count, size_t nthreads> void spawn_value_test() {
  std::printf("spawn_value_test()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  auto preTime = std::chrono::high_resolution_clock::now();
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
          Slot = co_await spawn(std::move(t));
          // in this case, the spawned function returns immediately,
          // and a 2nd co_await is required
          Slot = co_await std::move(
            co_await spawn(std::function([Slot]() -> task<size_t> {
              return [](size_t InnerSlot) -> task<size_t> {
                co_await yield();
                co_await yield();
                std::printf("func 3\t");
                co_return InnerSlot + 1;
              }(Slot);
            }))
          );
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
  auto postTime = std::chrono::high_resolution_clock::now();
  future.wait();
  auto doneTime = std::chrono::high_resolution_clock::now();

  auto spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawnDur.count(), spawnDur.count() / Count
  );

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, execDur.count(), execDur.count() / Count,
    nthreads * execDur.count() / Count
  );
}

template <size_t Count, size_t nthreads> void spawn_many_test() {
  std::printf("spawn_many_test()...\n");
  ex_cpu executor;
  executor.set_thread_count(nthreads).init();
  auto preTime = std::chrono::high_resolution_clock::now();
  auto future = post_bulk_waitable(
    executor,
    iter_adapter(
      0,
      [](size_t slot) -> task<void> {
        //  TODO make spawn take an invokable
        //  instead of constructing std::function
        //  here
        std::printf("%" PRIu64 ": pre outer\n", slot);
        task<size_t> t = [](size_t Slot) -> task<size_t> {
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
        spawn_many<1>(&t3).detach();
        std::printf("%" PRIu64 ": post outer\n", slot);
        co_return;
      }
    ),
    0, Count
  );
  auto postTime = std::chrono::high_resolution_clock::now();
  future.wait();
  auto doneTime = std::chrono::high_resolution_clock::now();

  auto spawnDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(postTime - preTime);
  std::printf(
    "spawned %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64 " ns/task\n", Count,
    spawnDur.count(), spawnDur.count() / Count
  );

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(doneTime - postTime);
  std::printf(
    "executed %" PRIu64 " tasks in %" PRIu64 " ns: %" PRIu64
    " ns/task (wall), %" PRIu64 " "
    "ns/task/thread\n",
    Count, execDur.count(), execDur.count() / Count,
    nthreads * execDur.count() / Count
  );
}

// Coerce a task into a coroutine_handle to erase its promise type
// This simulates an external coro type that TMC doesn't understand
std::coroutine_handle<> external_coro_test_task(int I) {
  return [](int i) -> task<void> {
    std::printf("external_coro_test_task(%d)...\n", i);
    co_return;
  }(I);
}

void external_coro_test() {
  std::printf("external_coro_test()...\n");
  ex_cpu executor;
  executor.init();
  tmc::post(executor, external_coro_test_task(1), 0);
  tmc::post_bulk(executor, tmc::iter_adapter(2, external_coro_test_task), 0, 2);
  tmc::post_waitable(
    executor,
    []() -> task<void> {
      tmc::spawn(external_coro_test_task(7)).detach();
      co_await tmc::spawn(external_coro_test_task(4));
    }(),
    0
  )
    .wait();
  tmc::post_waitable(
    executor,
    []() -> task<void> {
      tmc::spawn_many<2>(tmc::iter_adapter(8, external_coro_test_task))
        .detach();
      co_await tmc::spawn_many<2>(tmc::iter_adapter(5, external_coro_test_task)
      );
    }(),
    0
  )
    .wait();
}

int main() {
  small_func_spawn_bench_lazy<32000, 16>();
  large_task_spawn_bench_lazy<32000, 16>();
  large_task_spawn_bench_lazy_bulk<32000, 16>();
  prio_reversal_test<320, 16, 63>();
  co_await_eager_test<1, 16>();
  spawn_test<1, 16>();
  spawn_value_test<1, 16>();
  spawn_many_test<1, 16>();
  external_coro_test();
}

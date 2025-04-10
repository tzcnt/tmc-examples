// Based on https://hez2010.github.io/async-runtimes-benchmarks-2024/
// How much memory do 1 million tasks waiting on individual timers use?

// Two implementations:
// - wait_on_timers will wait on timers directly
// - wait_on_tasks will wait on tasks that contain timers

// On my machine, wait_on_timers uses 325MB for 1M tasks
// and            wait_on_tasks  uses 486MB for 1M tasks

// So most of the overhead is coming from the asio timer itself.
// Perhaps someday I can write a smaller timer.
#ifdef _WIN32
#include <SDKDDKVer.h>
#endif

#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <vector>

asio::steady_timer sleep_timer(ptrdiff_t seconds) {
  return asio::steady_timer{
    tmc::asio_executor(), std::chrono::seconds(seconds)
  };
}

tmc::task<int> wait_on_tasks(size_t TaskCount) {
  std::vector<tmc::task<void>> tasks;
  for (size_t i = 0; i < TaskCount; ++i) {
    tasks.emplace_back([]() -> tmc::task<void> {
      co_await sleep_timer(10).async_wait(tmc::aw_asio);
    }());
  }
  co_await tmc::spawn_many(tasks.begin(), TaskCount);
  co_return 0;
}

tmc::task<int> wait_on_timers(size_t TaskCount) {
  std::vector<asio::steady_timer> timers;
  for (size_t i = 0; i < TaskCount; ++i) {
    timers.emplace_back(sleep_timer(10));
  }
  co_await tmc::spawn_many(
    std::ranges::views::transform(
      timers,
      [](asio::steady_timer& timer) -> auto {
        return timer.async_wait(tmc::aw_asio);
      }
    ).begin(),
    TaskCount
  );
  co_return 0;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#ifndef NDEBUG
  // Hardcode the size in debug mode so we don't have to fuss around with input
  // arguments in the debug config.
  size_t taskCount = 30;
#else
  if (argc != 2) {
    printf("Usage: asio_timer_mem_bench <number of tasks>\n");
    exit(0);
  }

  size_t taskCount = static_cast<size_t>(atoi(argv[1]));
#endif
  tmc::asio_executor().init();
  return tmc::async_main(wait_on_timers(taskCount));
  // return tmc::async_main(wait_on_tasks(taskCount));
}

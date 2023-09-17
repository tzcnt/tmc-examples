#define TMC_IMPL
#include "tmc/ex_cpu.hpp"
#include "tmc/run_task.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/task.hpp"
#include <atomic>
#include <chrono>
#include <coroutine>
#include <iostream>
#include <thread>
using namespace tmc;

task<size_t> fib(size_t n) {
  if (n < 2)
    co_return n;
  // std::array<task<size_t>, 2> tasks;
  // tasks[0] = fib(n - 2);
  // tasks[1] = fib(n - 1);
  // auto results = co_await spawn_many<2>(tasks.data(), 0);
  // co_return results[0] + results[1];

  // co_await spawn_many<2>(iter_adapter(n - 2, fib), 0);

  // std::array<size_t, 2> results;
  // results[0] = co_await fib(n - 2);
  // results[1] = co_await fib(n - 1);
  // co_return results[0] + results[1];

  auto xt = spawn_early(fib(n - 1));
  auto y = co_await fib(n - 2);
  auto x = co_await xt;
  x = co_await xt;
  co_return x + y;
}

task<void> top_fib(size_t n) {
  auto result = co_await fib(n);
  std::printf("%ld\n", result);
  co_return;
}

constexpr size_t NRUNS = 1;
int main(int argc, char *argv[]) {
  if (argc != 2) {
    printf("Usage: fib <n-th fibonacci number requested>\n");
    exit(0);
  }

  size_t n = atoi(argv[1]);
  tmc::async_main([](int n) -> tmc::task<int> {
    auto start_time = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < NRUNS; ++i) {
      co_await top_fib(n), 0;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto total_time_us = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time);
    std::printf("%ld us\n", total_time_us.count() / NRUNS);
    co_return 0;
  }(n));
}

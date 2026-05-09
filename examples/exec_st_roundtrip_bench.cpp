// A benchmark for the latency and/or bandwidth of single-threaded or
// serializing executors. Tasks are posted from tmc::cpu_executor() into the
// single threaded executor, and then awaited. Sweeps from 1 to N producers,
// where N is the number of cores on the machine.

#include "tmc/all_headers.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#define NELEMS 1000000

static tmc::task<void> consumer([[maybe_unused]] int i) {
  // std::printf("%d", i);
  co_return;
}

template <typename Exec>
static tmc::task<void> producer(Exec& ex, size_t count) {
  // Single task ping-pong latency
  for (size_t i = 0; i < count; ++i) {
    co_await tmc::spawn(consumer(static_cast<int>(i))).run_on(ex);
  }

  // // Single task post, bulk await
  // auto fg = tmc::fork_group();
  // for (size_t i = 0; i < count; ++i) {
  //   fg.fork(consumer(static_cast<int>(i)), ex);
  // }
  // co_await std::move(fg);
}

static std::string formatWithCommas(size_t n) {
  auto s = std::to_string(n);
  int i = static_cast<int>(s.length()) - 3;
  while (i > 0) {
    s.insert(static_cast<size_t>(i), ",");
    i -= 3;
  }
  return s;
}

template <typename Exec>
tmc::task<size_t> run_bench(Exec& ex, size_t prodCount) {
  size_t per_task = NELEMS / prodCount;
  size_t rem = NELEMS % prodCount;
  std::vector<tmc::task<void>> prod(prodCount);
  for (size_t i = 0; i < prodCount; ++i) {
    size_t count = i < rem ? per_task + 1 : per_task;
    prod[i] = producer(ex, count);
  }

  auto startTime = std::chrono::high_resolution_clock::now();
  co_await tmc::spawn_many(prod);

  auto endTime = std::chrono::high_resolution_clock::now();

  size_t durMs = static_cast<size_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
      .count()
  );
  size_t elementsPerSec = static_cast<size_t>(
    static_cast<double>(NELEMS) * 1000.0 / static_cast<double>(durMs)
  );
  std::printf(" %s\t|", formatWithCommas(elementsPerSec).c_str());
  co_return durMs;
}

int main() {
  tmc::cpu_executor().set_spins(100).init();
  return tmc::async_main([]() -> tmc::task<int> {
    size_t threadCount = tmc::cpu_executor().thread_count();
    std::printf(
      "ex_st_roundtrip_bench: sweep 1 to %zu producers | %s elements | output "
      "units: tasks/sec\n",
      threadCount, formatWithCommas(NELEMS).c_str()
    );
    std::printf(
      "| prods  \t| ex_cpu_st\t|"
    );
    std::printf(
      "\n| ------------- | ------------- |"
    );

    tmc::ex_cpu_st excst;
    excst.set_spins(100).init();

    size_t total = 0;

    for (size_t prodCount = 1; prodCount <= threadCount; ++prodCount) {
      std::printf("\n| %zu prod\t|", prodCount);
      total += co_await run_bench(excst, prodCount);
    }
    std::printf("\n\ntotals:\n");
    double overallSec = static_cast<double>(total) / 1000.0;
    std::printf(" %.2f sec  ", overallSec);
    std::printf("\n");
    co_return 0;
  }());
}

// A benchmark for the throughput of tmc::ex_braid.
// Similar to the chan_bench example, but effectively always has 1 consumer.
// Sweeps from 1 to 10 producers.

#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/utils.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#define NELEMS 10000000

static tmc::task<void> consumer([[maybe_unused]] int i) {
  // std::printf("%d", i);
  co_return;
}

static tmc::task<void> producer(tmc::ex_braid& q, size_t count) {
  co_await tmc::spawn_many(tmc::iter_adapter(0, consumer), count).run_on(q);
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

int main() {
  tmc::cpu_executor().init();
  std::printf(
    "braid_bench: %zu threads | %s elements\n",
    tmc::cpu_executor().thread_count(), formatWithCommas(NELEMS).c_str()
  );
  return tmc::async_main([]() -> tmc::task<int> {
    auto overallStart = std::chrono::high_resolution_clock::now();

    for (size_t prodCount = 1; prodCount <= 10; ++prodCount) {
      tmc::ex_braid q;
      size_t per_task = NELEMS / prodCount;
      size_t rem = NELEMS % prodCount;
      std::vector<tmc::task<void>> prod(prodCount);
      for (size_t i = 0; i < prodCount; ++i) {
        size_t count = i < rem ? per_task + 1 : per_task;
        prod[i] = producer(q, count);
      }

      auto startTime = std::chrono::high_resolution_clock::now();
      co_await tmc::spawn_many(prod);

      auto endTime = std::chrono::high_resolution_clock::now();

      size_t execDur = static_cast<size_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
          endTime - startTime
        )
          .count()
      );
      double durMs = static_cast<double>(execDur) / 1000.0;
      size_t elementsPerSec =
        static_cast<size_t>(static_cast<double>(NELEMS) * 1000.0 / durMs);
      std::printf(
        "%zu prod\t %.2f ms\t%s elements/sec\n", prodCount, durMs,
        formatWithCommas(elementsPerSec).c_str()
      );
    }

    auto overallEnd = std::chrono::high_resolution_clock::now();
    size_t overallDur =
      static_cast<size_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                            overallEnd - overallStart
      )
                            .count());
    double overallSec = static_cast<double>(overallDur) / 1000000.0;
    std::printf("overall: %.2f sec\n", overallSec);

    co_return 0;
  }());
}

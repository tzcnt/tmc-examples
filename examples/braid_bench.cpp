// A benchmark for the throughput of tmc::ex_braid.
// Similar to the chan_bench example, but effectively always has 1 consumer.
// Sweeps from 1 to 10 producers.

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cstdio>
#include <string>

#define NELEMS 10000000

tmc::task<void> consumer(int i) {
  // std::printf("%d", i);
  co_return;
}

tmc::task<void> producer(tmc::ex_braid& q, size_t count) {
  co_await tmc::spawn_many(tmc::iter_adapter(0, consumer), count).run_on(q);
}

std::string formatWithCommas(size_t n) {
  auto s = std::to_string(n);
  int i = s.length() - 3;
  while (i > 0) {
    s.insert(i, ",");
    i -= 3;
  }
  return s;
}

int main() {
  tmc::cpu_executor().init();
  std::printf(
    "braid_bench: %zu threads | %s elements",
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
      co_await tmc::spawn_many(prod.data(), prod.size());

      auto endTime = std::chrono::high_resolution_clock::now();

      size_t execDur = std::chrono::duration_cast<std::chrono::microseconds>(
                         endTime - startTime
      )
                         .count();
      float durMs = static_cast<float>(execDur) / 1000.0f;
      size_t elementsPerSec =
        static_cast<size_t>(static_cast<float>(NELEMS) * 1000.0f / durMs);
      std::printf(
        "%zu prod\t %.2f ms\t%s elements/sec\n", prodCount, durMs,
        formatWithCommas(elementsPerSec).c_str()
      );
    }

    auto overallEnd = std::chrono::high_resolution_clock::now();
    size_t overallDur = std::chrono::duration_cast<std::chrono::microseconds>(
                          overallEnd - overallStart
    )
                          .count();
    float overallSec = static_cast<float>(overallDur) / 1000000.0f;
    std::printf("overall: %.2f sec\n", overallSec);

    co_return 0;
  }());
}

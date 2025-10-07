// A benchmark for the throughput of tmc::chan.
// Sweeps from 1 to 10 producers and 1 to 10 consumers.

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include "mpmcq.h"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#define NELEMS 10000000

queue_t queue;

tmc::task<void> producer(size_t count, size_t base) {
  // It would be more efficient to call `chan.post_bulk()`,
  // but for this benchmark we test pushing 1 at a time.
  for (size_t i = 0; i < count; ++i) {
    size_t v = base + i;
    enqueue(&queue, &v);
  }
  co_return;
}

struct result {
  size_t count;
  size_t sum;
};

tmc::task<result> consumer(size_t TotalCount) {
  size_t count = 0;
  size_t sum = 0;
  for (size_t i = 0; i < TotalCount; i++) {
    size_t v;
    dequeue(&queue, &v);
    sum += v;
    ++count;
  }
  co_return result{count, sum};
}

std::string formatWithCommas(size_t n) {
  auto s = std::to_string(n);
  int i = static_cast<int>(s.length()) - 3;
  while (i > 0) {
    s.insert(i, ",");
    i -= 3;
  }
  return s;
}

int main() {
  memset(&queue, 0, sizeof(queue_t));

  tmc::cpu_executor().init();
  std::printf(
    "chan_bench: %zu threads | %s elements\n",
    tmc::cpu_executor().thread_count(), formatWithCommas(NELEMS).c_str()
  );
  return tmc::async_main([]() -> tmc::task<int> {
    auto overallStart = std::chrono::high_resolution_clock::now();

    for (size_t consCount = 1; consCount <= 10; ++consCount) {
      for (size_t prodCount = 1; prodCount <= 10; ++prodCount) {
        size_t per_task = NELEMS / prodCount;
        size_t rem = NELEMS % prodCount;
        std::vector<tmc::task<void>> prod(prodCount);
        size_t base = 0;
        for (size_t i = 0; i < prodCount; ++i) {
          size_t count = i < rem ? per_task + 1 : per_task;
          prod[i] = producer(count, base);
          base += count;
        }
        std::vector<tmc::task<result>> cons(consCount);
        per_task = NELEMS / consCount;
        rem = NELEMS % consCount;
        for (size_t i = 0; i < consCount; ++i) {
          size_t count = i < rem ? per_task + 1 : per_task;
          cons[i] = consumer(count);
        }
        auto startTime = std::chrono::high_resolution_clock::now();
        auto c = tmc::spawn_many(cons).fork();
        co_await tmc::spawn_many(prod);

        auto consResults = co_await std::move(c);

        auto endTime = std::chrono::high_resolution_clock::now();

        size_t count = 0;
        size_t sum = 0;
        for (size_t i = 0; i < consResults.size(); ++i) {
          count += consResults[i].count;
          sum += consResults[i].sum;
        }
        if (count != NELEMS) {
          std::printf(
            "FAIL: Expected %zu elements but consumed %zu elements\n",
            static_cast<size_t>(NELEMS), count
          );
        }

        size_t expectedSum = 0;
        for (size_t i = 0; i < NELEMS; ++i) {
          expectedSum += i;
        }
        if (sum != expectedSum) {
          std::printf(
            "FAIL: Expected %zu sum but got %zu sum\n", expectedSum, sum
          );
        }

        size_t execDur = std::chrono::duration_cast<std::chrono::microseconds>(
                           endTime - startTime
        )
                           .count();

        float durMs = static_cast<float>(execDur) / 1000.0f;
        size_t elementsPerSec =
          static_cast<size_t>(static_cast<float>(NELEMS) * 1000.0f / durMs);
        std::printf(
          "%zu prod\t%zu cons\t %.2f ms\t%s elements/sec\n", prodCount,
          consCount, durMs, formatWithCommas(elementsPerSec).c_str()
        );
      }
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

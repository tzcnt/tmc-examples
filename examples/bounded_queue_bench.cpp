// A benchmark for the throughput of tmc::bounded_queue.
// Sweeps from 1 to 10 producers and 1 to 10 consumers.

#include "tmc/all_headers.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#define NELEMS 10000000

struct queue_config : tmc::bounded_queue_default_config {
  // Capacity must be a power of 2, and must stay above the maximum number of
  // concurrent producers and consumers because bounded_queue stores waiters in
  // the slots themselves.
  static inline constexpr size_t Capacity = 4096;
  static inline constexpr size_t PackingLevel = 1;
};
using queue_type = tmc::bounded_queue<size_t, queue_config>;

static tmc::task<void> producer(queue_type& queue, size_t count, size_t base) {
  for (size_t i = 0; i < count; ++i) {
    co_await queue.push(base + i);
  }
}

struct result {
  size_t count;
  size_t sum;
};

static tmc::task<result> consumer(queue_type& queue, size_t expected) {
  size_t count = 0;
  size_t sum = 0;

  for (; count < expected; ++count) {
    auto data = co_await queue.pull();
    sum += data;
  }

  co_return result{count, sum};
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
    "bounded_queue_bench: %zu threads | %s elements\n",
    tmc::cpu_executor().thread_count(), formatWithCommas(NELEMS).c_str()
  );
  return tmc::async_main([]() -> tmc::task<int> {
    auto overallStart = std::chrono::high_resolution_clock::now();

    for (size_t iter = 0; iter < 10; ++iter)
      for (size_t consCount = 1; consCount <= 1; ++consCount) {
        for (size_t prodCount = 1; prodCount <= 1; ++prodCount) {
          auto queue = queue_type{};
          size_t per_task = NELEMS / prodCount;
          size_t rem = NELEMS % prodCount;
          std::vector<tmc::task<void>> prod(prodCount);
          size_t base = 0;
          for (size_t i = 0; i < prodCount; ++i) {
            size_t count = i < rem ? per_task + 1 : per_task;
            prod[i] = producer(queue, count, base);
            base += count;
          }
          std::vector<tmc::task<result>> cons(consCount);
          for (size_t i = 0; i < consCount; ++i) {
            cons[i] = consumer(queue, NELEMS);
          }
          auto startTime = std::chrono::high_resolution_clock::now();
          auto c = tmc::spawn_many(cons).fork();
          co_await tmc::spawn_many(prod);

          // queue.close();
          // co_await queue.drain();
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
            "%zu prod\t%zu cons\t %.2f ms\t%s elements/sec\n", prodCount,
            consCount, durMs, formatWithCommas(elementsPerSec).c_str()
          );
        }
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

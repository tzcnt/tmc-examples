// A benchmark for the throughput of tmc::chan.
// Sweeps from 1 to 10 producers and 1 to 10 consumers.

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cstdio>

#define NELEMS 10000000

struct chan_config : tmc::chan_default_config {
  // static inline constexpr size_t BlockSize = 4096;
  // static inline constexpr size_t PackingLevel = 0;
};
using token = tmc::chan_tok<size_t, chan_config>;

tmc::task<void> producer(token chan, size_t count, size_t base) {
  for (size_t i = 0; i < count; ++i) {
    bool ok = co_await chan.push(base + i);
    assert(ok);
  }
}

struct result {
  size_t count;
  size_t sum;
};

tmc::task<result> consumer(token chan) {
  size_t count = 0;
  size_t sum = 0;
  auto data = co_await chan.pull();
  while (data.has_value()) {
    ++count;
    sum += data.value();
    data = co_await chan.pull();
  }
  co_return result{count, sum};
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
    "chan_bench: %zu threads | %s elements\n",
    tmc::cpu_executor().thread_count(), formatWithCommas(NELEMS).c_str()
  );
  return tmc::async_main([]() -> tmc::task<int> {
    auto overallStart = std::chrono::high_resolution_clock::now();

    for (size_t consCount = 1; consCount <= 10; ++consCount) {
      for (size_t prodCount = 1; prodCount <= 10; ++prodCount) {
        auto chan = tmc::make_channel<size_t, chan_config>();
        size_t per_task = NELEMS / prodCount;
        size_t rem = NELEMS % prodCount;
        std::vector<tmc::task<void>> prod(prodCount);
        size_t base = 0;
        for (size_t i = 0; i < prodCount; ++i) {
          size_t count = i < rem ? per_task + 1 : per_task;
          prod[i] = producer(chan, count, base);
          base += count;
        }
        std::vector<tmc::task<result>> cons(consCount);
        for (size_t i = 0; i < consCount; ++i) {
          cons[i] = consumer(chan);
        }
        auto startTime = std::chrono::high_resolution_clock::now();
        auto c = tmc::spawn_many(cons.data(), cons.size()).run_early();
        co_await tmc::spawn_many(prod.data(), prod.size());

        chan.close();
        chan.drain_wait();
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

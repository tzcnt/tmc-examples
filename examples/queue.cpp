#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cstdio>

#define NELEMS 10000000

struct channel_config : tmc::channel_default_config {
  // static inline constexpr size_t BlockSize = 4096;
  // static inline constexpr size_t ReuseBlocks = true;
  // static inline constexpr size_t ConsumerSpins = 0;
  // static inline constexpr size_t PackingLevel = 0;
  // static inline constexpr size_t HeavyLoadThreshold = 2000000;
};
using token = tmc::channel_token<size_t, channel_config>;

tmc::task<void> producer(token chan, size_t count, size_t base) {
  for (size_t i = 0; i < count; ++i) {
    auto err = co_await chan.push(base + i);

    assert(err == tmc::channel_error::OK);
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
  while (data.index() == 0) {
    ++count;
    sum += std::get<0>(data);
    data = co_await chan.pull();
  }
  // queue should be closed, not some other error
  assert(std::get<1>(data) == tmc::channel_error::CLOSED);
  co_return result{count, sum};
}

int main() {
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    for (size_t i = 0; i < 1; ++i) {
      auto overallStart = std::chrono::high_resolution_clock::now();
      for (size_t prodCount = 1; prodCount <= 10; ++prodCount) {
        for (size_t consCount = 1; consCount <= 10; ++consCount) {
          auto chan = tmc::make_channel<size_t, channel_config>();
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
          co_await chan.drain();
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

          size_t execDur =
            std::chrono::duration_cast<std::chrono::microseconds>(
              endTime - startTime
            )
              .count();
          std::printf(
            "%zu prod\t%zu cons\t %zu us\n", prodCount, consCount, execDur
          );
        }
      }

      auto overallEnd = std::chrono::high_resolution_clock::now();
      size_t overallDur = std::chrono::duration_cast<std::chrono::microseconds>(
                            overallEnd - overallStart
      )
                            .count();
      std::printf("overall: %zu us\n", overallDur);
    }
    co_return 0;
  }());
}

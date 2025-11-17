// A benchmark for the throughput of tmc::chan.
// Sweeps from 1 to 10 producers and 1 to 10 consumers.

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#define NELEMS 10000000

struct chan_config : tmc::chan_default_config {
  // static inline constexpr size_t BlockSize = 4096;
  // static inline constexpr size_t PackingLevel = 0;
  // static inline constexpr bool EmbedFirstBlock = false;
};
using token = tmc::chan_tok<size_t, chan_config>;

static tmc::task<void> producer(token chan, size_t count, size_t base) {
  // It would be more efficient to call `chan.post_bulk()`,
  // but for this benchmark we test pushing 1 at a time.
  for (size_t i = 0; i < count; ++i) {
    [[maybe_unused]] bool ok = co_await chan.push(base + i);
    assert(ok);
  }
}

struct result {
  size_t count;
  size_t sum;
};

static tmc::task<result> consumer(token chan) {
  size_t count = 0;
  size_t sum = 0;

  // pull() implementation
  auto data = co_await chan.pull();
  while (data.has_value()) {
    ++count;
    sum += data.value();
    data = co_await chan.pull();
  }
  co_return result{count, sum};

  // // try_pull() implementation
  // while (true) {
  //   auto data = chan.try_pull();
  //   switch (data.index()) {
  //   case tmc::chan_err::OK:
  //     ++count;
  //     sum += std::get<tmc::chan_err::OK>(data);
  //     break;
  //   case tmc::chan_err::EMPTY:
  //     // Spinning on try_pull() without yielding from an executor thread can
  //     // cause deadlock if producers are waiting to run. Calling reschedule()
  //     // ensures that those producers are able to run.
  //     co_await tmc::reschedule();
  //     break;
  //   case tmc::chan_err::CLOSED:
  //     co_return result{count, sum};
  //   default:
  //     break;
  //   }
  // }
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
  tmc::ex_cpu_st exec;
  exec.init();
  std::printf(
    "chan_bench: %zu threads | %s elements\n", exec.thread_count(),
    formatWithCommas(NELEMS).c_str()
  );
  return tmc::post_waitable(
           exec,
           []() -> tmc::task<int> {
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
                 auto c = tmc::spawn_many(cons).fork();
                 co_await tmc::spawn_many(prod);

                 // The call to close() is not necessary, but is included here
                 // for exposition.
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
                     "FAIL: Expected %zu sum but got %zu sum\n", expectedSum,
                     sum
                   );
                 }

                 size_t execDur = static_cast<size_t>(
                   std::chrono::duration_cast<std::chrono::microseconds>(
                     endTime - startTime
                   )
                     .count()
                 );

                 double durMs = static_cast<double>(execDur) / 1000.0;
                 size_t elementsPerSec = static_cast<size_t>(
                   static_cast<double>(NELEMS) * 1000.0 / durMs
                 );
                 std::printf(
                   "%zu prod\t%zu cons\t %.2f ms\t%s elements/sec\n", prodCount,
                   consCount, durMs, formatWithCommas(elementsPerSec).c_str()
                 );
               }
             }

             auto overallEnd = std::chrono::high_resolution_clock::now();
             size_t overallDur = static_cast<size_t>(
               std::chrono::duration_cast<std::chrono::microseconds>(
                 overallEnd - overallStart
               )
                 .count()
             );
             double overallSec = static_cast<double>(overallDur) / 1000000.0;
             std::printf("overall: %.2f sec\n", overallSec);
             co_return 0;
           }()
  )
    .get();
}

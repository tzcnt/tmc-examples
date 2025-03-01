#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/ticket_queue.hpp"

#include <cstdio>
#include <numeric>

#define NELEMS 10000
#define QUEUE_SIZE 16

using tmc::queue_error::CLOSED;
using tmc::queue_error::OK;

template <size_t Size>
tmc::task<void>
producer(tmc::ticket_queue<int, Size>& q, size_t count, size_t base) {
  for (size_t i = 0; i < count; ++i) {
    auto err = q.push(base + i);
    assert(!err);
  }
  co_return;
}

struct result {
  size_t count;
  size_t sum;
};

template <size_t Size>
tmc::task<result> consumer(tmc::ticket_queue<int, Size>& q) {
  size_t count = 0;
  size_t sum = 0;
  auto data = co_await q.pull();

  while (data.index() == OK) {
    ++count;
    sum += std::get<0>(data);
    data = co_await q.pull();
  }
  // queue should be closed, not some other error
  assert(data.index() == CLOSED);
  co_return result{count, sum};
}

int main() {
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    for (size_t i = 0; i < 100; ++i)
      for (size_t prodCount = 5; prodCount <= 5; ++prodCount) {
        for (size_t consCount = 5; consCount <= 5; ++consCount) {
          tmc::ticket_queue<int, QUEUE_SIZE> q;
          size_t per_task = NELEMS / prodCount;
          size_t rem = NELEMS % prodCount;
          std::vector<tmc::task<void>> prod(prodCount);
          size_t base = 0;
          for (size_t i = 0; i < prodCount; ++i) {
            size_t count = i < rem ? per_task + 1 : per_task;
            prod[i] = producer(q, count, base);
            base += count;
          }
          std::vector<tmc::task<result>> cons(consCount);
          for (size_t i = 0; i < consCount; ++i) {
            cons[i] = consumer(q);
          }

          auto startTime = std::chrono::high_resolution_clock::now();
          auto c = tmc::spawn_many(cons.data(), cons.size()).run_early();
          co_await tmc::spawn_many(prod.data(), prod.size());
          // std::this_thread::sleep_for(std::chrono::milliseconds(100));
          q.close();
          q.drain_sync();
          auto results = co_await std::move(c);

          auto endTime = std::chrono::high_resolution_clock::now();

          size_t count = 0;
          size_t sum = 0;
          for (size_t i = 0; i < results.size(); ++i) {
            count += results[i].count;
            sum += results[i].sum;
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
    co_return 0;
  }());
}

#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/fixed_queue.hpp"

#include <cstdio>

#define NELEMS 1000
#define QUEUE_SIZE 512

template <size_t Size>
tmc::task<void> producer(tmc::fixed_queue<int, Size>& q, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    auto err = co_await q.push(i);
    assert(!err);
  }
}

template <size_t Size>
tmc::task<void> consumer(tmc::fixed_queue<int, Size>& q) {
  auto data = co_await q.pull();

  while (data.index() == OK) {
    // std::printf("%d ", std::get<0>(data));
    data = co_await q.pull();
  }
  // queue should be closed, not some other error
  assert(data.index() == CLOSED);
}

int main() {
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    for (size_t prodCount = 1; prodCount <= 10; ++prodCount) {
      for (size_t consCount = 1; consCount <= 10; ++consCount) {
        tmc::fixed_queue<int, QUEUE_SIZE> q;
        size_t per_task = NELEMS / prodCount;
        size_t rem = NELEMS % prodCount;
        std::vector<tmc::task<void>> prod(prodCount);
        for (size_t i = 0; i < prodCount; ++i) {
          size_t count = i < rem ? per_task + 1 : per_task;
          prod[i] = producer(q, count);
        }
        std::vector<tmc::task<void>> cons(consCount);
        for (size_t i = 0; i < consCount; ++i) {
          cons[i] = consumer(q);
        }

        auto startTime = std::chrono::high_resolution_clock::now();
        auto c = tmc::spawn_many(cons.data(), cons.size()).run_early();
        co_await tmc::spawn_many(prod.data(), prod.size());
        q.close();
        co_await std::move(c);

        auto endTime = std::chrono::high_resolution_clock::now();

        size_t execDur = std::chrono::duration_cast<std::chrono::microseconds>(
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

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cstdio>

#define NELEMS 10000

tmc::task<void> consumer(int i) {
  // std::printf("%d", i);
  co_return;
}

tmc::task<void> producer(tmc::ex_braid& q, size_t count) {
  co_await tmc::spawn_many(tmc::iter_adapter(0, consumer), count).run_on(q);
}

int main() {
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    for (size_t prodCount = 1; prodCount <= 10; ++prodCount) {
      for (size_t consCount = 1; consCount <= 10; ++consCount) {
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
        std::printf(
          "%zu prod\t%zu cons\t %zu us\n", prodCount, consCount, execDur
        );
      }
    }
    co_return 0;
  }());
}

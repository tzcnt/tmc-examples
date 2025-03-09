#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/ticket_queue.hpp"

#include <cstdio>

#define NELEMS 1000000
#define QUEUE_SIZE 4096

using tmc::queue_error::CLOSED;
using tmc::queue_error::OK;

template <size_t Size>
tmc::task<void> producer(
  tmc::queue_handle<int, Size> q, size_t count, size_t base,
  tmc::tiny_lock& print_lock
) {
  std::array<size_t, 64> tids{};
  size_t localMigrations = 0;
  size_t distantMigrations = 0;
  size_t before = tmc::detail::this_thread::thread_index;
  for (size_t i = 0; i < count; ++i) {
    tids[before]++;
    auto err = q.push(base + i);
    size_t after = tmc::detail::this_thread::thread_index;
    if (after != before) {
      if ((after & 0xFFC0) != (before & 0xFFC0)) {
        distantMigrations++;
      } else {
        localMigrations++;
      }
      before = after;
    }
    assert(!err);
  }
  {
    tmc::tiny_lock_guard lg(print_lock);

    std::printf(
      "PRODUCER local: %zu distant: %zu  |  ", localMigrations,
      distantMigrations
    );
    for (size_t j = 0; j < 64; ++j) {
      std::printf("push tids: ");
      for (size_t i = 0; i < 64; ++i) {
        std::printf("%zu ", tids[i]);
      }
      std::printf("\n");
      break;
    }
  }
  co_return;
}

struct result {
  size_t count;
  size_t sum;
};

template <size_t Size>
tmc::task<result>
consumer(tmc::queue_handle<int, Size> q, tmc::tiny_lock& print_lock) {
  size_t count = 0;
  size_t sum = 0;
  size_t suspended = 0;
  size_t succeeded = 0;
  size_t localMigrations = 0;
  size_t distantMigrations = 0;
  std::array<size_t, 64> tids{};
  size_t before = tmc::detail::this_thread::thread_index;
  tids[before]++;
  auto data = co_await q.pull();
  {
    size_t after = tmc::detail::this_thread::thread_index;
    if (after != before) {
      if ((after & 0xFFFC) != (before & 0xFFFC)) {
        distantMigrations++;
      } else {
        localMigrations++;
      }
      before = after;
    }
  }
  while (data.index() == OK) {
    ++count;
    int val = std::get<0>(data);
    sum += val & ~((1 << 31));
    if ((val & (1 << 31)) != 0) {
      ++suspended;
    } else {
      ++succeeded;
    }
    tids[before]++;
    data = co_await q.pull();
    size_t after = tmc::detail::this_thread::thread_index;
    if (after != before) {
      if ((after & 0xFFFC) != (before & 0xFFFC)) {
        distantMigrations++;
      } else {
        localMigrations++;
      }
      before = after;
    }
  }
  // queue should be closed, not some other error
  assert(data.index() == CLOSED);
  // std::printf("DONE\n");
  // std::fflush(stdout);
  {
    tmc::tiny_lock_guard lg(print_lock);
    std::printf(
      "CONSUMER local: %zu distant: %zu  |  ", localMigrations,
      distantMigrations
    );
    std::printf("succeeded: %zu suspended: %zu  |  ", succeeded, suspended);
    for (size_t j = 0; j < 64; ++j) {
      std::printf("pull tids: ");
      for (size_t i = 0; i < 64; ++i) {
        std::printf("%zu ", tids[i]);
      }
      std::printf("\n");
      break;
    }
  }
  co_return result{count, sum};
}

int main() {
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    tmc::tiny_lock print_lock;
    for (size_t i = 0; i < 1; ++i) {
      for (size_t prodCount = 2; prodCount <= 2; ++prodCount) {
        for (size_t consCount = 10; consCount <= 10; ++consCount) {
          auto q = tmc::queue_handle<int, QUEUE_SIZE>::make();
          size_t per_task = NELEMS / prodCount;
          size_t rem = NELEMS % prodCount;
          std::vector<tmc::task<void>> prod(prodCount);
          size_t base = 0;
          for (size_t i = 0; i < prodCount; ++i) {
            size_t count = i < rem ? per_task + 1 : per_task;
            prod[i] = producer(q, count, base, print_lock);
            base += count;
          }
          std::vector<tmc::task<result>> cons(consCount);
          for (size_t i = 0; i < consCount; ++i) {
            cons[i] = consumer(q, print_lock);
          }

          auto startTime = std::chrono::high_resolution_clock::now();
          std::array<tmc::task<void>, 100> dummy;
          for (size_t j = 0; j < dummy.size(); ++j) {
            dummy[j] = []() -> tmc::task<void> { co_return; }();
          }
          tmc::spawn_many(dummy.begin(), dummy.end()).detach();
          auto c = tmc::spawn_many(cons.data(), cons.size()).run_early();
          // std::this_thread::sleep_for(std::chrono::milliseconds(100));
          co_await tmc::spawn_many(prod.data(), prod.size());

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
    }
    co_return 0;
  }());
}

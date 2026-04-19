// A benchmark for the latency and/or bandwidth of single-threaded or
// serializing executors. Tasks are posted from tmc::cpu_executor() into the
// single threaded executor, and then awaited. Sweeps from 1 to N producers,
// where N is the number of cores on the machine.

#include "tmc/all_headers.hpp"
#include "tmc/asio/ex_asio.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

#define NELEMS 1000000

#ifndef MUTEX_IMPL
#define MUTEX_IMPL 0
#endif

static size_t sum = 0;

static tmc::task<void> consumer([[maybe_unused]] int i) {
  // std::printf("%d", i);
  sum += static_cast<size_t>(i);
  co_return;
}

// A mutex is faster than the serializing executors - perhaps because mutex is
// LIFO/unfair and the others are FIFO/fair
[[maybe_unused]] static tmc::task<void>
mutex_producer(tmc::mutex& mut, size_t count) {
  // Single task ping-pong latency
  for (size_t i = 0; i < count; ++i) {
    auto scope = co_await mut.lock_scope();
    co_await consumer(static_cast<int>(i));
  }
}

[[maybe_unused]] static tmc::task<void>
mutex_co_unlock_producer(tmc::mutex& mut, size_t count) {
  // Single task ping-pong latency
  for (size_t i = 0; i < count; ++i) {
    co_await mut;
    co_await consumer(static_cast<int>(i));
    co_await mut.co_unlock();
  }
}

[[maybe_unused]] static tmc::task<void>
one_shot_mutex_producer(tmc::one_shot_mutex& mut, size_t count) {
  // Single task ping-pong latency
  for (size_t i = 0; i < count; ++i) {
    co_await mut;
    co_await consumer(static_cast<int>(i));
  }
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

template <typename Mut>
tmc::task<size_t> run_bench(Mut& mut, size_t prodCount) {
  if (sum != 0) {
    std::fprintf(stderr, "sum not reset at bench start: got %zu\n", sum);
    std::abort();
  }

  size_t per_task = NELEMS / prodCount;
  size_t rem = NELEMS % prodCount;
  size_t expectedSum = 0;
  std::vector<tmc::task<void>> prod(prodCount);
  for (size_t i = 0; i < prodCount; ++i) {
    size_t count = i < rem ? per_task + 1 : per_task;
    expectedSum += count * (count - 1) / 2;
#if MUTEX_IMPL == 1
    prod[i] = one_shot_mutex_producer(mut, count);
#elif MUTEX_IMPL == 2
    prod[i] = mutex_co_unlock_producer(mut, count);
#elif MUTEX_IMPL == 0
    prod[i] = mutex_producer(mut, count);
#else
#error "Unknown MUTEX_IMPL value"
#endif
  }

  auto startTime = std::chrono::high_resolution_clock::now();
  co_await tmc::spawn_many(prod);

  auto endTime = std::chrono::high_resolution_clock::now();

  if (sum != expectedSum) {
    std::fprintf(
      stderr, "sum mismatch: expected %zu, got %zu\n", expectedSum, sum
    );
    std::abort();
  }
  sum = 0;

  size_t durMs = static_cast<size_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime)
      .count()
  );
  size_t elementsPerSec = static_cast<size_t>(
    static_cast<double>(NELEMS) * 1000.0 / static_cast<double>(durMs)
  );
  std::printf(" %s\t|", formatWithCommas(elementsPerSec).c_str());
  co_return durMs;
}

int main() {
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    size_t threadCount = tmc::cpu_executor().thread_count();
    std::printf(
      "ex_st_roundtrip_bench: sweep 1 to %zu producers | %s elements | output "
      "units: tasks/sec\n",
      threadCount, formatWithCommas(NELEMS).c_str()
    );
    std::printf("| producers\t| tasks/sec\t|");
    std::printf("\n| ------------- | ------------- |");

    size_t total = 0;

#if MUTEX_IMPL == 1
    tmc::one_shot_mutex mut;
#else
    tmc::mutex mut;
#endif

    for (size_t prodCount = 1; prodCount <= threadCount; ++prodCount) {
      std::printf("\n| %zu prod\t|", prodCount);
      total += co_await run_bench(mut, prodCount);
    }
    std::printf("\n\ntotal:\n");
    double overallSec = static_cast<double>(total) / 1000.0;
    std::printf(" %.2f sec  ", overallSec);
    std::printf("\n");
    co_return 0;
  }());
}

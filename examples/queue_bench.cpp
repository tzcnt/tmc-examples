// A benchmark for the throughput of tmc::qu_spsc_unbounded / mpsc.

#include "tmc/all_headers.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#define NELEMS 10000000
#define PRODUCER_COUNT 1
#define CONSUMER_COUNT 1

static_assert(
  CONSUMER_COUNT == 1, "This benchmark requires a single consumer."
);

#if PRODUCER_COUNT == 1
struct queue_config : tmc::qu_spsc_unbounded_default_config {
  //  static inline constexpr bool ConsumerCanSuspend = true;
  //  static inline constexpr size_t BlockSize = 4096;
  //  static inline constexpr size_t PackingLevel = 1;
  //  static inline constexpr bool EmbedFirstBlock = false;
};
using queue_t = tmc::qu_spsc_unbounded<size_t, queue_config>;
#else
struct queue_config : tmc::qu_mpsc_unbounded_default_config {
  //  static inline constexpr bool ConsumerCanSuspend = true;
  //  static inline constexpr size_t BlockSize = 4096;
  //  static inline constexpr size_t PackingLevel = 0;
  //  static inline constexpr bool EmbedFirstBlock = false;
};
using queue_t = tmc::qu_mpsc_unbounded<size_t, queue_config>;
#endif

struct producer_result {
  size_t duration_us;
};

static tmc::task<producer_result>
producer(queue_t& queue, size_t count, size_t base) {
  auto startTime = std::chrono::high_resolution_clock::now();
  // It would be more efficient to call `queue.post_bulk()`,
  // but for this benchmark we test pushing 1 at a time.
  for (size_t i = 0; i < count; ++i) {
    queue.post(base + i);
  }
  auto endTime = std::chrono::high_resolution_clock::now();
  co_return producer_result{static_cast<size_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
      .count()
  )};
}

struct result {
  size_t count;
  size_t sum;
};

[[maybe_unused]] static tmc::task<result>
consumer(queue_t& queue, size_t expected_count) {
  size_t count = 0;
  size_t sum = 0;

  while (count < expected_count) {
    auto data = co_await queue.pull();
    ++count;
    sum += *data;
  }

  co_return result{count, sum};
}

[[maybe_unused]] static tmc::task<result>
consumer_try_pull(queue_t& queue, size_t expected_count) {
  size_t count = 0;
  size_t sum = 0;

  while (count < expected_count) {
    auto data = queue.try_pull();
    if (data) {
      ++count;
      sum += *data;
    } else {
      TMC_CPU_PAUSE();
      co_await tmc::reschedule();
    }
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
  tmc::ex_any* prodExPtr;
#if PRODUCER_COUNT == 1
  // Use a single-threaded producer
  tmc::ex_cpu_st prodEx;
#ifdef TMC_USE_HWLOC
  // Pin producer to physical core 1
  tmc::topology::topology_filter f;
  f.set_core_indexes({1});
  prodEx.add_partition(f);
#endif
  prodEx.init();
  prodExPtr = prodEx.type_erased();
#else
  // Use a multi-threaded producer
  tmc::ex_cpu prodEx;
#ifdef TMC_USE_HWLOC
  // Pin producer to physical cores adjacent to the consumer.
  // Consumer is on core 0, so producers start from core 1
  tmc::topology::topology_filter f;
  std::vector<size_t> allowedCores;
  for (size_t i = 1; i <= PRODUCER_COUNT; ++i) {
    allowedCores.push_back(i);
  }
  f.set_core_indexes(allowedCores);
  prodEx.add_partition(f);
#endif
  prodEx.set_thread_count(PRODUCER_COUNT).init();
  prodExPtr = prodEx.type_erased();
#endif
  std::printf(
    "qu_mpsc SPSC bench: %s elements\n", formatWithCommas(NELEMS).c_str()
  );
  return tmc::post_waitable(
           prodExPtr,
           []() -> tmc::task<int> {
             tmc::ex_cpu_st consEx;
#ifdef TMC_USE_HWLOC
             // Pin consumer to physical core 0
             tmc::topology::topology_filter f2;
             f2.set_core_indexes({0});
             consEx.add_partition(f2);
#endif
             consEx.init();

             queue_t queue;
             size_t per_task = NELEMS / PRODUCER_COUNT;
             size_t rem = NELEMS % PRODUCER_COUNT;

             // Construct producer and consumer tasks but don't initiate yet
             std::vector<tmc::task<producer_result>> prod(PRODUCER_COUNT);
             size_t base = 0;
             for (size_t i = 0; i < PRODUCER_COUNT; ++i) {
               size_t count = i < rem ? per_task + 1 : per_task;
               prod[i] = producer(queue, count, base);
               base += count;
             }
             std::vector<tmc::task<result>> cons(CONSUMER_COUNT);
             cons[0] = consumer(queue, NELEMS);

             auto startTime = std::chrono::high_resolution_clock::now();
             // Start consumers
             auto c = tmc::spawn_many(cons).run_on(consEx).fork();
             // Start producers and wait for them to finish
             auto prodResults = co_await tmc::spawn_many(prod);
             // Wait for consumers to finish
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
             size_t maxProducerDuration = 0;
             for (size_t i = 0; i < prodResults.size(); ++i) {
               if (maxProducerDuration < prodResults[i].duration_us) {
                 maxProducerDuration = prodResults[i].duration_us;
               }
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
               "%zu prod\t%zu cons\t %.2f ms\t%s elements/sec\n",
               static_cast<size_t>(PRODUCER_COUNT),
               static_cast<size_t>(CONSUMER_COUNT), durMs,
               formatWithCommas(elementsPerSec).c_str()
             );
             std::printf(
               "producer max: %.2f ms\n",
               static_cast<double>(maxProducerDuration) / 1000.0
             );
             co_return 0;
           }()
  )
    .get();
}

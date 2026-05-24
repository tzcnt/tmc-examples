// A benchmark for the throughput of tmc::qu_unbounded_mpsc in SPSC mode.

#include "tmc/all_headers.hpp"
#include "tmc/aw_yield.hpp"
#include "tmc/qu_unbounded_mpsc.hpp"
#include "tmc/topology.hpp"

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#define NELEMS 10000000
static constexpr size_t PRODUCER_COUNT = 2;
static constexpr size_t CONSUMER_COUNT = 1;

struct queue_config : tmc::qu_unbounded_mpsc_default_config {
  static inline constexpr bool ConsumerCanSuspend = true;
  // static inline constexpr size_t BlockSize = 4096;
  // static inline constexpr size_t PackingLevel = 0;
  // static inline constexpr bool EmbedFirstBlock = false;
};
using queue_t = tmc::qu_unbounded_mpsc<size_t, queue_config>;

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
    // if (i % 16384 == 0) {
    //   co_await tmc::reschedule();
    // }
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

static tmc::task<result> consumer(queue_t& queue, size_t expected_count) {
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
#ifdef TMC_USE_HWLOC
  tmc::topology::topology_filter f;
  f.set_group_indexes({0});
  tmc::cpu_executor().add_partition(f);
#endif
  tmc::cpu_executor().init();
  std::printf(
    "qu_mpsc SPSC bench: %zu threads | %s elements\n",
    tmc::cpu_executor().thread_count(), formatWithCommas(NELEMS).c_str()
  );
  return tmc::async_main([]() -> tmc::task<int> {
    auto overallStart = std::chrono::high_resolution_clock::now();

    queue_t queue;
    size_t per_task = NELEMS / PRODUCER_COUNT;
    size_t rem = NELEMS % PRODUCER_COUNT;
    std::vector<tmc::task<producer_result>> prod(PRODUCER_COUNT);
    size_t base = 0;
    for (size_t i = 0; i < PRODUCER_COUNT; ++i) {
      size_t count = i < rem ? per_task + 1 : per_task;
      prod[i] = producer(queue, count, base);
      base += count;
    }
    auto startTime = std::chrono::high_resolution_clock::now();
    std::vector<tmc::task<result>> cons(CONSUMER_COUNT);
    cons[0] = consumer(queue, NELEMS);
    auto c = tmc::spawn_many(cons).fork();
    auto prodResults = co_await tmc::spawn_many(prod);
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
      std::printf("FAIL: Expected %zu sum but got %zu sum\n", expectedSum, sum);
    }
    size_t maxProducerDuration = 0;
    for (size_t i = 0; i < prodResults.size(); ++i) {
      if (maxProducerDuration < prodResults[i].duration_us) {
        maxProducerDuration = prodResults[i].duration_us;
      }
    }

    size_t execDur = static_cast<size_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime)
        .count()
    );

    double durMs = static_cast<double>(execDur) / 1000.0;
    size_t elementsPerSec =
      static_cast<size_t>(static_cast<double>(NELEMS) * 1000.0 / durMs);
    std::printf(
      "%zu prod\t%zu cons\t %.2f ms\t%s elements/sec\n", PRODUCER_COUNT,
      CONSUMER_COUNT, durMs, formatWithCommas(elementsPerSec).c_str()
    );
    std::printf(
      "producer max: %.2f ms\n",
      static_cast<double>(maxProducerDuration) / 1000.0
    );

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

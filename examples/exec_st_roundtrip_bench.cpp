// A benchmark for the latency and/or bandwidth of single-threaded or
// serializing executors. Tasks are posted from tmc::cpu_executor() into the
// single threaded executor, and then awaited. Sweeps from 1 to N producers,
// where N is the number of cores on the machine.

#include "tmc/all_headers.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/topology.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#define NELEMS 1000000

static tmc::task<void> consumer([[maybe_unused]] int i) {
  // std::printf("%d", i);
  co_return;
}

template <typename Exec>
static tmc::task<void> producer(Exec& ex, size_t count) {
  // Single task ping-pong latency
  for (size_t i = 0; i < count; ++i) {
    co_await tmc::spawn(consumer(static_cast<int>(i))).run_on(ex);
  }

  // // Single task post, bulk await
  // auto fg = tmc::fork_group();
  // for (size_t i = 0; i < count; ++i) {
  //   fg.fork(consumer(static_cast<int>(i)), ex);
  // }
  // co_await std::move(fg);
}

// A mutex is faster than the serializing executors - perhaps because mutex is
// LIFO/unfair and the others are FIFO/fair
static tmc::task<void> mutex_producer(tmc::mutex& mut, size_t count) {
  // Single task ping-pong latency
  for (size_t i = 0; i < count; ++i) {
    auto scope = co_await mut.lock_scope();
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

template <typename Exec, bool Mutex = false>
tmc::task<size_t> run_bench(Exec& ex, size_t prodCount) {
  size_t per_task = NELEMS / prodCount;
  size_t rem = NELEMS % prodCount;
  std::vector<tmc::task<void>> prod(prodCount);
  for (size_t i = 0; i < prodCount; ++i) {
    size_t count = i < rem ? per_task + 1 : per_task;
    if constexpr (Mutex) {
      prod[i] = mutex_producer(ex, count);
    } else {
      prod[i] = producer(ex, count);
    }
  }

  auto startTime = std::chrono::high_resolution_clock::now();
  co_await tmc::spawn_many(prod);

  auto endTime = std::chrono::high_resolution_clock::now();

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
  // Run the producer-side using the most efficient executor (single-threaded)
  // so we are mostly benchmarking the consumer-side round trip.
  tmc::ex_cpu_st producer_ex;
#ifdef TMC_USE_HWLOC
  // If possible, bind all of the executors to the same L3 cache for consistent
  // latency.
  tmc::topology::topology_filter group0;
  group0.set_group_indexes({0});
  producer_ex.add_partition(group0);
#endif
  // Spin to keep executors alive long enough for the ping-pong. If we don't do
  // this, executors with faster queues are punished, because they go to sleep
  // more quickly after completing their work.
  producer_ex.set_spins(50);
  producer_ex.init();
  tmc::post_waitable(
    producer_ex,
    [&]() -> tmc::task<int> {
      size_t maxProducers = 64;
      std::printf(
        "ex_st_roundtrip_bench: sweep 1 to %zu producers | %s elements | "
        "output "
        "units: tasks/sec\n",
        maxProducers, formatWithCommas(NELEMS).c_str()
      );
      std::printf(
        "| prods  \t| ex_cpu(1)\t| ex_cpu_st\t| ex_braid\t| ex_asio\t| "
        "tmc::mutex\t|"
      );
      std::printf(
        "\n| ------------- | ------------- | ------------- | ------------- | "
        "------------- | ------------- |"
      );

      tmc::ex_cpu exc;
#ifdef TMC_USE_HWLOC
      exc.add_partition(group0);
#endif
      exc.set_spins(50);
      exc.set_thread_count(1).init();

      tmc::ex_cpu_st excst;
#ifdef TMC_USE_HWLOC
      excst.add_partition(group0);
#endif
      excst.set_spins(50);
      excst.init();

      tmc::ex_braid exbr;

      tmc::ex_asio exasio;
#ifdef TMC_USE_HWLOC
      exasio.add_partition(group0);
#endif
      exasio.init();

      tmc::mutex mut;

      std::array<size_t, 5> totals{};

      for (size_t prodCount = 1; prodCount <= maxProducers; ++prodCount) {
        std::printf("\n| %zu prod\t|", prodCount);
        totals[0] += co_await run_bench(exc, prodCount);
        totals[1] += co_await run_bench(excst, prodCount);
        totals[2] += co_await run_bench(exbr, prodCount);
        totals[3] += co_await run_bench(exasio, prodCount);
        totals[4] += co_await run_bench<tmc::mutex, true>(mut, prodCount);
      }
      std::printf("\n\ntotals:\n");
      for (size_t i = 0; i < totals.size(); ++i) {
        double overallSec = static_cast<double>(totals[i]) / 1000.0;
        std::printf(" %.2f sec  ", overallSec);
      }
      std::printf("\n");
      co_return 0;
    }()
  )
    .wait();
}

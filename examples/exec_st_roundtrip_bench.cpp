// A benchmark for the latency and/or bandwidth of single-threaded executors.
// Tasks are dispatched from tmc::cpu_executor() into the single threaded
// executor, and then awaited. Sweeps from 1 to N producers, where N is the
// number of cores on the machine.

#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/asio/ex_asio.hpp"

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
  // Single task round trip latency
  for (size_t i = 0; i < count; ++i) {
    co_await tmc::spawn(consumer(static_cast<int>(i))).run_on(ex);
  }

  // // Single task dispatch, bulk await
  // auto fg = tmc::fork_group();
  // for (size_t i = 0; i < count; ++i) {
  //   fg.fork(consumer(static_cast<int>(i)), ex);
  // }
  // co_await std::move(fg);
}

// A mutex is faster than the serializing executors - perhaps because mutex is
// LIFO/unfair and the others are FIFO/fair
static tmc::task<void> mutex_producer(tmc::mutex& mut, size_t count) {
  // Single task round trip latency
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
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    size_t threadCount = tmc::cpu_executor().thread_count();
    std::printf(
      "ex_st_roundtrip_bench: sweep 1 to %zu producers | %s elements | output "
      "units: tasks/sec\n",
      threadCount, formatWithCommas(NELEMS).c_str()
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
    exc.set_thread_count(1).init();

    tmc::ex_cpu_st excst;
    excst.init();

    tmc::ex_braid exbr;

    tmc::ex_asio exasio;
    exasio.init();

    tmc::mutex mut;

    std::array<size_t, 5> totals{};

    for (size_t prodCount = 1; prodCount <= threadCount; ++prodCount) {
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
  }());
}

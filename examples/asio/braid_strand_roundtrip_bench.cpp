// A benchmark for serialized round-trips running on tmc::asio_executor().
// Compares tmc::ex_braid against asio::strand while sweeping from 1 to N
// logical producers, where N is the number of cpu executor worker threads.

#include "tmc/all_headers.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/detail/thread_locals.hpp"

#ifdef TMC_USE_BOOST_ASIO
#include <boost/asio/strand.hpp>

namespace asio = boost::asio;
#else
#include <asio/strand.hpp>
#endif

#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

constexpr size_t NELEMS = 1000000;

template <typename Strand> struct strand_executor {
  Strand* strand;
  tmc::ex_any type_erased_this;

  static void post_erased(
    void* erased, tmc::work_item&& item, size_t priority, size_t thread_hint
  ) noexcept {
    static_cast<strand_executor*>(erased)
      ->post(std::move(item), priority, thread_hint);
  }

  static void post_bulk_erased(
    void* erased, tmc::work_item* items, size_t count, size_t priority,
    size_t thread_hint
  ) noexcept {
    auto& ex = *static_cast<strand_executor*>(erased);
    for (size_t i = 0; i < count; ++i) {
      ex.post(std::move(items[i]), priority, thread_hint);
    }
  }

  explicit strand_executor(Strand* strand)
      : strand(strand), type_erased_this() {
    type_erased_this.executor = this;
    type_erased_this.s_post = &post_erased;
    type_erased_this.s_post_bulk = &post_bulk_erased;
  }

  tmc::ex_any* type_erased() noexcept { return &type_erased_this; }

  void post(
    tmc::work_item&& item, size_t priority = 0,
    [[maybe_unused]] size_t thread_hint = 0
  ) noexcept {
    strand->post(
      [this, priority, item = std::move(item)]() mutable {
        auto* prev_executor = tmc::detail::this_thread::executor();
        tmc::detail::this_thread::this_task().prio = priority;
        tmc::detail::this_thread::executor() = &type_erased_this;
        item();
        tmc::detail::this_thread::executor() = prev_executor;
      },
      std::allocator<void>{}
    );
  }
};

namespace tmc::detail {
template <typename Strand> struct executor_traits<strand_executor<Strand>> {
  static void post(
    strand_executor<Strand>& ex, tmc::work_item&& item, size_t priority,
    size_t thread_hint
  ) {
    ex.post(std::move(item), priority, thread_hint);
  }

  static tmc::ex_any* type_erased(strand_executor<Strand>& ex) {
    return ex.type_erased();
  }
};
} // namespace tmc::detail

static tmc::task<void> consumer([[maybe_unused]] int i) { co_return; }

static std::string format_with_commas(size_t n) {
  auto s = std::to_string(n);
  int i = static_cast<int>(s.length()) - 3;
  while (i > 0) {
    s.insert(static_cast<size_t>(i), ",");
    i -= 3;
  }
  return s;
}

static tmc::task<void> braid_producer(tmc::ex_braid& braid, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    co_await tmc::spawn(consumer(static_cast<int>(i))).run_on(braid);
  }
}

template <typename StrandExec>
static tmc::task<void> strand_producer(StrandExec& strand_exec, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    co_await tmc::spawn(consumer(static_cast<int>(i))).run_on(strand_exec);
  }
}

template <typename ProducerFactory>
static tmc::task<size_t>
run_bench(ProducerFactory&& make_producer, size_t prod_count) {
  size_t per_task = NELEMS / prod_count;
  size_t rem = NELEMS % prod_count;
  std::vector<tmc::task<void>> producers(prod_count);

  for (size_t i = 0; i < prod_count; ++i) {
    size_t count = i < rem ? per_task + 1 : per_task;
    producers[i] = make_producer(count);
  }

  auto start_time = std::chrono::high_resolution_clock::now();
  co_await tmc::spawn_many(producers);
  auto end_time = std::chrono::high_resolution_clock::now();

  size_t dur_ms = static_cast<size_t>(
    std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time)
      .count()
  );
  size_t elements_per_sec = static_cast<size_t>(
    static_cast<double>(NELEMS) * 1000.0 / static_cast<double>(dur_ms)
  );
  std::printf(" %s\t|", format_with_commas(elements_per_sec).c_str());
  co_return dur_ms;
}

int main() {
  tmc::asio_executor().init();

  return tmc::async_main([]() -> tmc::task<int> {
    co_await tmc::resume_on(tmc::asio_executor());

    size_t producer_limit = tmc::cpu_executor().thread_count();
    std::printf(
      "asio_braid_strand_roundtrip_bench: sweep 1 to %zu producers | %s "
      "elements | output units: tasks/sec\n",
      producer_limit, format_with_commas(NELEMS).c_str()
    );
    std::printf("| prods\t| ex_braid\t| asio::strand\t|");
    std::printf("\n| ------------- | ------------- | ------------- |");

    tmc::ex_braid braid(tmc::asio_executor());
    asio::strand<tmc::ex_asio::executor_type> strand{
      tmc::asio_executor().ioc.get_executor()
    };
    strand_executor strand_exec{&strand};

    std::array<size_t, 2> totals{};

    for (size_t prod_count = 1; prod_count <= producer_limit; ++prod_count) {
      std::printf("\n| %zu prod\t|", prod_count);
      totals[0] += co_await run_bench(
        [&](size_t count) { return braid_producer(braid, count); }, prod_count
      );
      totals[1] += co_await run_bench(
        [&](size_t count) { return strand_producer(strand_exec, count); },
        prod_count
      );
    }

    std::printf("\n\ntotals:\n");
    for (size_t total : totals) {
      std::printf(" %.2f sec  ", static_cast<double>(total) / 1000.0);
    }
    std::printf("\n");
    co_return 0;
  }());
}

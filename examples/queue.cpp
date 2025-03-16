#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cstdio>
#include <thread>

#define NELEMS 1000000

static inline constexpr bool PRINT = true;

struct channel_config : tmc::channel_default_config {
  // static inline constexpr size_t BlockSize = 4096;
  // static inline constexpr size_t ReuseBlocks = true;
  // static inline constexpr size_t ConsumerSpins = 0;
  // static inline constexpr size_t PackingLevel = 0;
};
using token = tmc::channel_token<int, channel_config>;

enum kind { CONS, PROD };
struct result {
  size_t count;
  size_t sum;
  size_t suspended = 0;
  size_t succeeded = 0;
  size_t localMigrations = 0;
  size_t distantMigrations = 0;
  kind kind;
  std::array<size_t, 64> tids;
  result() : count{}, sum{}, tids{} {}

  void print() {
    std::printf(
      "%s local: %zu distant: %zu  |  ", kind == CONS ? "CONSUMER" : "PRODUCER",
      localMigrations, distantMigrations
    );
    if (kind == CONS) {
      std::printf("succeeded: %zu suspended: %zu  |  ", succeeded, suspended);
    }
    size_t modeCount = 0;
    size_t mode = 0;
    for (size_t j = 0; j < 64; ++j) {
      std::printf("tids: ");
      for (size_t i = 0; i < 64; ++i) {
        std::printf("%zu ", tids[i]);
        if (tids[i] > modeCount) {
          mode = i;
          modeCount = tids[i];
        }
      }
      std::printf("mode: %zu", mode);
      std::printf("\n");
      break;
    }
  }
};

tmc::task<result> producer(token chan, size_t count, size_t base) {
  result res{};
  size_t localMigrations = 0;
  size_t distantMigrations = 0;
  size_t before = tmc::detail::this_thread::thread_index;
  for (size_t i = 0; i < count; ++i) {
    res.tids[before]++;
    auto err = co_await chan.push(base + i);
    size_t after = tmc::detail::this_thread::thread_index;
    if (after != before) {
      if ((after & 0xFFFC) != (before & 0xFFFC)) {
        distantMigrations++;
      } else {
        localMigrations++;
      }
      before = after;
    }
    assert(err == tmc::channel_error::OK);
  }

  res.count = count;
  res.localMigrations = localMigrations;
  res.distantMigrations = distantMigrations;
  res.kind = PROD;
  co_return res;
}

tmc::task<result> consumer(token chan) {
  size_t count = 0;
  size_t sum = 0;
  size_t suspended = 0;
  size_t succeeded = 0;
  size_t localMigrations = 0;
  size_t distantMigrations = 0;
  size_t before = tmc::detail::this_thread::thread_index;
  result res{};
  res.tids[before]++;
  auto data = co_await chan.pull();
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
  while (data.index() == 0) {
    ++count;
    int val = std::get<0>(data);
    sum += val & ~((1 << 31));
    if ((val & (1 << 31)) != 0) {
      ++suspended;
    } else {
      ++succeeded;
    }
    res.tids[before]++;
    data = co_await chan.pull();
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
  assert(std::get<1>(data) == tmc::channel_error::CLOSED);

  res.count = count;
  res.sum = sum;
  res.suspended = suspended;
  res.succeeded = succeeded;
  res.localMigrations = localMigrations;
  res.distantMigrations = distantMigrations;
  res.kind = CONS;
  co_return res;
}

int main() {
  tmc::cpu_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    for (size_t i = 0; i < 1000; ++i) {
      // std::this_thread::sleep_for(std::chrono::milliseconds(100));
      for (size_t prodCount = 2; prodCount <= 2; ++prodCount) {
        for (size_t consCount = 10; consCount <= 10; ++consCount) {
          auto chan = tmc::make_channel<int, channel_config>();
          size_t per_task = NELEMS / prodCount;
          size_t rem = NELEMS % prodCount;
          std::vector<tmc::task<result>> prod(prodCount);
          size_t base = 0;
          for (size_t i = 0; i < prodCount; ++i) {
            size_t count = i < rem ? per_task + 1 : per_task;
            prod[i] = producer(chan, count, base);
            base += count;
          }
          std::vector<tmc::task<result>> cons(consCount);
          for (size_t i = 0; i < consCount; ++i) {
            cons[i] = consumer(chan);
          }
          auto startTime = std::chrono::high_resolution_clock::now();
          // std::array<tmc::task<void>, 100> dummy;
          // for (size_t j = 0; j < dummy.size(); ++j) {
          //   dummy[j] = []() -> tmc::task<void> { co_return; }();
          // }
          // tmc::spawn_many(dummy.begin(), dummy.end()).detach();
          auto c = tmc::spawn_many(cons.data(), cons.size()).run_early();
          // std::this_thread::sleep_for(std::chrono::milliseconds(100));
          auto prodResults = co_await tmc::spawn_many(prod.data(), prod.size());

          chan.close();
          co_await chan.drain();
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

          size_t execDur =
            std::chrono::duration_cast<std::chrono::microseconds>(
              endTime - startTime
            )
              .count();
          std::printf(
            "%zu prod\t%zu cons\t %zu us\n", prodCount, consCount, execDur
          );
          // if (execDur > 100000) {
          if constexpr (PRINT) {
            for (size_t i = 0; i < prodResults.size(); ++i) {
              prodResults[i].print();
            }
            for (size_t i = 0; i < consResults.size(); ++i) {
              consResults[i].print();
            }
          }
          co_return 0;
          //}
        }
      }
    }
    co_return 0;
  }());
}

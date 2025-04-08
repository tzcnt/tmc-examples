// Fuzz tester for tmc::channel

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include "xoshiro.hpp"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>

constexpr size_t ELEMS_PER_ACTION = 1000;
constexpr size_t ACTION_COUNT = 1000;

xso::rng prng;
size_t base;
std::atomic<size_t> full_sum;
std::atomic<size_t> producers_started;
std::atomic<size_t> producers_done;
std::atomic<size_t> consumers_started;
std::atomic<size_t> consumers_done;

void reset() {
  base = 0;
  full_sum = 0;
  producers_started = 0;
  producers_done = 0;
  consumers_started = 0;
  consumers_done = 0;
}

struct chan_config : tmc::chan_default_config {
  static inline constexpr size_t BlockSize = 128;
  // static inline constexpr size_t PackingLevel = 0;
  // static inline constexpr size_t EmbedFirstBlock = false;
};
using token = tmc::chan_tok<size_t, chan_config>;

tmc::task<void> producer(token Chan, size_t Base, size_t Count) {
  for (size_t i = 0; i < Count; ++i) {
    Chan.post(Base + i);
  }
  producers_done.fetch_add(1);
  co_return;
}

tmc::task<void> consumer(token chan, size_t Count) {
  size_t sum = 0;
  for (size_t i = 0; i < Count; ++i) {
    auto data = co_await chan.pull();
    if (data.has_value()) {
      sum += data.value();
    }
  }
  full_sum.fetch_add(sum);
  consumers_done.fetch_add(1);
}

int choose_action() { return prng.sample(0, 1); }

tmc::task<void> do_action(token& Chan) {
  int action = choose_action();
  switch (action) {
  case 0: {
    producers_started.fetch_add(1);
    size_t count = prng.sample(1, 1000);
    tmc::spawn(producer(Chan, base, count)).detach();
    base += count;
    break;
  }
  case 1:
    consumers_started.fetch_add(1);
    tmc::spawn(consumer(Chan, prng.sample(1, 1000))).detach();
    break;
  default:
    std::terminate();
  }
  co_return;
}

void wait_for_producers_to_finish() {
  size_t p = producers_started.load();
  for (size_t d = producers_done.load(std::memory_order_relaxed); d < p;
       d = producers_done.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

void wait_for_consumers_to_finish() {
  size_t p = consumers_started.load();
  for (size_t d = consumers_done.load(std::memory_order_relaxed); d < p;
       d = consumers_done.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
}

TEST(test_fuzz_chan, test_fuzz_chan) {
  reset();
  tmc::async_main([]() -> tmc::task<int> {
    auto chan = tmc::make_channel<size_t, chan_config>();
    for (size_t tick = 0; tick < ACTION_COUNT; ++tick) {
      co_await do_action(chan);
    }
    // Requires re-running the test to get a different value.
    // Pass --gtest_repeat=N on the command line.
    bool wait_on_producers = (0 == prng.sample(0, 1));
    if (wait_on_producers) {
      wait_for_producers_to_finish();
    }
    chan.close();
    // Drain remaining data
    {
      size_t sum = 0;
      auto data = co_await chan.pull();
      while (data.has_value()) {
        sum += data.value();
        data = co_await chan.pull();
      }
      full_sum.fetch_add(sum);
    }
    // Just wake the remaining waiting consumers
    co_await chan.drain();
    wait_for_consumers_to_finish();

    if (wait_on_producers) {
      size_t expectedSum = 0;
      for (size_t i = 0; i < base; ++i) {
        expectedSum += i;
      }
      EXPECT_EQ(expectedSum, full_sum.load());
    }
    co_return 0;
  }());
}

int main(int argc, char** argv) {
  prng.seed();
  tmc::cpu_executor().init();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

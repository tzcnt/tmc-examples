// Fuzz tester for tmc::channel

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include "xoshiro.hpp"
#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <exception>
#include <ranges>

constexpr int ELEMS_PER_ACTION = 1000;
constexpr size_t ACTION_COUNT = 1000;

static xso::rng prng;
static size_t base;
static std::atomic<size_t> full_sum;

static void reset() {
  base = 0;
  full_sum = 0;
}

struct chan_config : tmc::chan_default_config {
  static inline constexpr size_t BlockSize = 128;
  // static inline constexpr size_t PackingLevel = 0;
  // static inline constexpr size_t EmbedFirstBlock = false;
};
using token = tmc::chan_tok<size_t, chan_config>;

static tmc::task<void> producer(token Chan, size_t Base, size_t Count) {
  for (size_t i = 0; i < Count; ++i) {
    Chan.post(Base + i);
  }
  co_return;
}

static tmc::task<void> bulk_producer(token Chan, size_t Base, size_t Count) {
  Chan.post_bulk(std::ranges::views::iota(Base, Base + Count));
  co_return;
}

static tmc::task<void> consumer(token chan, size_t Count) {
  size_t sum = 0;
  for (size_t i = 0; i < Count; ++i) {
    auto data = co_await chan.pull();
    if (data.has_value()) {
      sum += data.value();
    }
  }
  full_sum.fetch_add(sum);
}

static tmc::task<void> try_pull_consumer(token chan, size_t Count) {
  size_t sum = 0;
  for (size_t i = 0; i < Count; ++i) {
    auto data = chan.try_pull();
    switch (data.index()) {
    case tmc::chan_err::OK:
      sum += std::get<0>(data);
      break;
    case tmc::chan_err::EMPTY:
      co_await tmc::reschedule();
      break;
    case tmc::chan_err::CLOSED:
      break;
    default:
      break;
    }
  }

  full_sum.fetch_add(sum);
}

static int choose_action() { return prng.sample(0, 3); }

static tmc::task<void> do_action(
  token& Chan, tmc::aw_fork_group<0, void>& Producers,
  tmc::aw_fork_group<0, void>& Consumers
) {
  int action = choose_action();
  switch (action) {
  case 0: {
    size_t count = static_cast<size_t>(prng.sample(1, ELEMS_PER_ACTION));
    Producers.fork(producer(Chan, base, count));
    base += count;
    break;
  }
  case 1: {
    size_t count = static_cast<size_t>(
      prng.sample(0, 5)
    ); // include 0 to test posting empty range
    Producers.fork(bulk_producer(Chan, base, count));
    base += count;
    break;
  }
  case 2:
    Consumers.fork(
      consumer(Chan, static_cast<size_t>(prng.sample(1, ELEMS_PER_ACTION)))
    );
    break;
  case 3:
    Consumers.fork(try_pull_consumer(
      Chan, static_cast<size_t>(prng.sample(1, ELEMS_PER_ACTION))
    ));
    break;
  default:
    std::terminate();
  }
  co_return;
}

static auto run_one_test(bool wait_on_producers) -> tmc::task<int> {
  auto chan = tmc::make_channel<size_t, chan_config>();
  auto producers = tmc::fork_group();
  auto consumers = tmc::fork_group();
  for (size_t tick = 0; tick < ACTION_COUNT; ++tick) {
    co_await do_action(chan, producers, consumers);
  }
  if (wait_on_producers) {
    co_await std::move(producers);
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
  co_await std::move(consumers);

  if (wait_on_producers) {
    size_t expectedSum = 0;
    for (size_t i = 0; i < base; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, full_sum.load());
  } else {
    // If we didn't wait for them before, we still have to wait now
    // to ensure no tasks leak.
    co_await std::move(producers);
  }
  co_return 0;
}

TEST(test_fuzz_chan, test_fuzz_chan_wait) {
  reset();
  tmc::async_main(run_one_test(true));
}

TEST(test_fuzz_chan, test_fuzz_chan_nowait) {
  reset();
  tmc::async_main(run_one_test(false));
}

int main(int argc, char** argv) {
  prng.seed();
  tmc::cpu_executor().init();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

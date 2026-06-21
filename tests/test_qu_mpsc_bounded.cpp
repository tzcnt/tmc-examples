#include "test_common.hpp"
#include "tmc/ex_cpu_st.hpp"
#include "tmc/qu_mpsc_bounded.hpp"

#include <atomic>
#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

#define CATEGORY test_qu_mpsc_bounded

namespace {

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().set_thread_count(4).init(); }
  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }
  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static constexpr size_t MPSC_TEST_SENTINEL = static_cast<size_t>(-1);
static constexpr size_t TEST_CAPACITY = 128;

template <size_t Pack> struct qu_config : tmc::qu_mpsc_bounded_default_config {
  static inline constexpr size_t PackingLevel = Pack;
  static inline constexpr bool ConsumerCanSuspend = true;
};

// `destructor_counter` is provided by test_common.hpp.

// Basic single-producer / single-consumer behavior. This is the simplest
// configuration of an MPSC queue (no producer contention) and verifies the
// fast paths work end-to-end.
template <size_t PackingLevel, typename Executor> void do_basic_test(Executor& Exec) {
  test_async_main(Exec, []() -> tmc::task<void> {
    {
      // general test - single push/pull
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<PackingLevel>>{TEST_CAPACITY};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; ++i) {
            bool ok = co_await Chan.push(i);
            EXPECT_TRUE(ok);
          }
          bool ok = co_await Chan.push(MPSC_TEST_SENTINEL);
          EXPECT_TRUE(ok);
          co_return i;
        }(chan),
        [](auto& Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          while (true) {
            auto v = co_await Chan.pull();
            if (*v == MPSC_TEST_SENTINEL) {
              co_return result{count, sum};
            }
            ++count;
            sum += *v;
          }
        }(chan)
      );
      auto& prod = std::get<0>(results);
      auto& cons = std::get<1>(results);
      EXPECT_EQ(NITEMS, prod);
      EXPECT_EQ(NITEMS, cons.count);
      size_t expectedSum = 0;
      for (size_t i = 0; i < NITEMS; ++i) {
        expectedSum += i;
      }
      EXPECT_EQ(expectedSum, cons.sum);
    }
    {
      // destroy queue with data remaining inside (consumer never drained)
      std::atomic<size_t> count{0};
      {
        auto chan = tmc::qu_mpsc_bounded<destructor_counter, qu_config<PackingLevel>>{
          TEST_CAPACITY
        };
        for (size_t i = 0; i < 12; ++i) {
          bool ok = co_await chan.push(destructor_counter{&count});
          EXPECT_TRUE(ok);
        }

        for (size_t i = 0; i < 7; ++i) {
          auto v = chan.try_pull();
          EXPECT_TRUE(static_cast<bool>(v));
        }

        EXPECT_EQ(count.load(), 7);
      }
      // Now chan goes out of scope; remaining data's destructors are called
      EXPECT_EQ(count.load(), 12);
    }
  }());
}

TEST_F(CATEGORY, config_sweep) {
  do_basic_test<0>(ex());
  do_basic_test<1>(ex());
}

// Multiple producers feeding a single consumer. Per the design's weak-ordering
// semantics, the consumer sees items in CAS order (not push-call order), so
// we verify by sum rather than order.
TEST_F(CATEGORY, many_producers) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NPRODUCERS = 16;
    static constexpr size_t ITEMS_PER_PRODUCER = 500;
    static constexpr size_t TOTAL = NPRODUCERS * ITEMS_PER_PRODUCER;

    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{32};

    std::vector<tmc::task<void>> producers;
    producers.reserve(NPRODUCERS);
    for (size_t p = 0; p < NPRODUCERS; ++p) {
      producers.emplace_back([](auto& Chan, size_t Base) -> tmc::task<void> {
        for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
          bool ok = co_await Chan.push(Base + i);
          EXPECT_TRUE(ok);
        }
        co_return;
      }(chan, p * ITEMS_PER_PRODUCER));
    }

    auto results = co_await tmc::spawn_tuple(
      tmc::spawn_many(producers.data(), producers.size()),
      [](auto& Chan) -> tmc::task<size_t> {
        size_t sum = 0;
        for (size_t i = 0; i < TOTAL; ++i) {
          auto v = co_await Chan.pull();
          sum += *v;
        }
        co_return sum;
      }(chan)
    );

    size_t expectedSum = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, std::get<1>(results));
  }());
}

// Capacity = 1: each push blocks until the previous one is drained. This
// is the highest-contention configuration: every producer must park.
TEST_F(CATEGORY, tiny_capacity_many_producers) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NPRODUCERS = 8;
    static constexpr size_t ITEMS_PER_PRODUCER = 200;
    static constexpr size_t TOTAL = NPRODUCERS * ITEMS_PER_PRODUCER;

    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{1};

    std::vector<tmc::task<void>> producers;
    producers.reserve(NPRODUCERS);
    for (size_t p = 0; p < NPRODUCERS; ++p) {
      producers.emplace_back([](auto& Chan, size_t Base) -> tmc::task<void> {
        for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
          bool ok = co_await Chan.push(Base + i);
          EXPECT_TRUE(ok);
        }
        co_return;
      }(chan, p * ITEMS_PER_PRODUCER));
    }

    auto results = co_await tmc::spawn_tuple(
      tmc::spawn_many(producers.data(), producers.size()),
      [](auto& Chan) -> tmc::task<size_t> {
        size_t sum = 0;
        for (size_t i = 0; i < TOTAL; ++i) {
          auto v = co_await Chan.pull();
          sum += *v;
        }
        co_return sum;
      }(chan)
    );

    size_t expectedSum = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, std::get<1>(results));
  }());
}

// try_pull on an empty queue yields EMPTY; after data arrives, it yields OK.
TEST_F(CATEGORY, try_pull_basic) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_bounded_err;
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};

    {
      auto v = chan.try_pull();
      EXPECT_EQ(qerr::EMPTY, v.status());
      EXPECT_FALSE(static_cast<bool>(v));
    }

    static constexpr size_t NITEMS = 10;
    for (size_t i = 0; i < NITEMS; ++i) {
      bool ok = co_await chan.push(i);
      EXPECT_TRUE(ok);
    }

    size_t sum = 0;
    size_t count = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      auto v = chan.try_pull();
      EXPECT_EQ(qerr::OK, v.status());
      sum += *v;
      ++count;
    }
    EXPECT_EQ(NITEMS, count);
    size_t expectedSum = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, sum);

    {
      auto v = chan.try_pull();
      EXPECT_EQ(qerr::EMPTY, v.status());
    }
    co_return;
  }());
}

// A type with no default / copy / move constructor. Can only be created
// in-place via push()'s emplace forwarding.
struct non_movable {
  int value;

  non_movable(int X, int Y) noexcept : value{X + Y} {}

  non_movable() = delete;
  non_movable(non_movable const&) = delete;
  non_movable(non_movable&&) = delete;
  non_movable& operator=(non_movable const&) = delete;
  non_movable& operator=(non_movable&&) = delete;
};

TEST_F(CATEGORY, non_movable_type) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<non_movable, qu_config<0>>{TEST_CAPACITY};

    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    bool ok;
    ok = co_await chan.push(1, 2);
    EXPECT_TRUE(ok);
    ok = co_await chan.push(3, 4);
    EXPECT_TRUE(ok);
    ok = co_await chan.push(5, 6);
    EXPECT_TRUE(ok);

    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(3, v->value);
    }

    {
      auto v = co_await chan.pull();
      EXPECT_EQ(7, v->value);
    }

    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(11, v->value);
    }

    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    co_return;
  }());
}

// try_pull on a closed, empty queue returns CLOSED status.
TEST_F(CATEGORY, try_pull_closed_empty) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_bounded_err;
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};
    EXPECT_TRUE(chan.empty());
    chan.close();

    // A closed-and-drained queue is not considered empty; the consumer should
    // pull and observe the CLOSED status.
    EXPECT_FALSE(chan.empty());

    auto v = chan.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close() is idempotent.
TEST_F(CATEGORY, close_idempotent) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};
    chan.close();
    chan.close();
    chan.close();
    co_return;
  }());
}

// pull() after close() returns an empty scope.
TEST_F(CATEGORY, pull_after_close_returns_empty) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};
    chan.close();

    auto v = co_await chan.pull();
    EXPECT_FALSE(static_cast<bool>(v));

    auto v2 = co_await chan.pull();
    EXPECT_FALSE(static_cast<bool>(v2));
    co_return;
  }());
}

// close() wakes a consumer suspended at the cutoff offset.
TEST_F(CATEGORY, close_wakes_suspended_consumer) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};

    auto consumer = tmc::spawn([](auto& Chan) -> tmc::task<bool> {
                      auto v = co_await Chan.pull();
                      co_return static_cast<bool>(v);
                    }(chan))
                      .fork();

    // Force the consumer to run first and suspend on the empty queue.
    co_await tmc::reschedule();
    chan.close();

    bool got_value = co_await std::move(consumer);
    EXPECT_FALSE(got_value); // consumer woken by close with empty scope
    co_return;
  }());
}

// Drain pre-close data, then observe CLOSED.
TEST_F(CATEGORY, drain_then_close) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_bounded_err;
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};

    static constexpr size_t NITEMS = 50;
    for (size_t i = 0; i < NITEMS; ++i) {
      bool ok = co_await chan.push(i);
      EXPECT_TRUE(ok);
    }
    chan.close();

    // Drain via pull().
    size_t sum = 0;
    size_t count = 0;
    while (true) {
      auto v = co_await chan.pull();
      if (!v) {
        break;
      }
      sum += *v;
      ++count;
    }
    EXPECT_EQ(NITEMS, count);
    size_t expectedSum = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, sum);

    // Subsequent try_pull yields CLOSED.
    auto v = chan.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    co_return;
  }());
}

// close_resume_inline() with no waiting consumer behaves like close().
TEST_F(CATEGORY, close_resume_inline_no_waiting_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_bounded_err;
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};

    for (size_t i = 0; i < 5; ++i) {
      bool ok = co_await chan.push(i);
      EXPECT_TRUE(ok);
    }

    chan.close_resume_inline();

    size_t sum = 0;
    size_t count = 0;
    while (true) {
      auto v = chan.try_pull();
      if (v.status() == qerr::OK) {
        sum += *v;
        ++count;
      } else if (v.status() == qerr::CLOSED) {
        break;
      } else {
        ADD_FAILURE() << "expected OK or CLOSED";
        break;
      }
    }
    EXPECT_EQ(5u, count);
    EXPECT_EQ(0u + 1u + 2u + 3u + 4u, sum);
    co_return;
  }());
}

// push() after close() deterministically completes synchronously with false.
TEST_F(CATEGORY, push_after_close_returns_false) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};

    chan.close();

    bool ok = co_await chan.push(size_t{123});
    EXPECT_FALSE(ok);

    auto v = co_await chan.pull();
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close_resume_inline() resumes a consumer that is already suspended.
TEST_F(CATEGORY, close_resume_inline_wakes_suspended_consumer) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};

    auto consumer = tmc::spawn([](auto& Chan) -> tmc::task<bool> {
                      auto v = co_await Chan.pull();
                      co_return static_cast<bool>(v);
                    }(chan))
                      .fork();

    // Force the consumer to run first and suspend on the empty queue.
    co_await tmc::reschedule();
    chan.close_resume_inline();

    bool got_value = co_await std::move(consumer);
    EXPECT_FALSE(got_value);
    co_return;
  }());
}

// close() may land on a physical slot that still holds pre-close data.
TEST_F(CATEGORY, close_with_data_at_cutoff_slot) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_bounded_err;
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{1};

    bool ok = co_await chan.push(size_t{7});
    EXPECT_TRUE(ok);
    chan.close();

    {
      auto v = chan.try_pull();
      EXPECT_EQ(qerr::OK, v.status());
      EXPECT_EQ(7u, *v);
    }
    {
      auto v = chan.try_pull();
      EXPECT_EQ(qerr::CLOSED, v.status());
    }
    co_return;
  }());
}

// Verify that ConsumerCanSuspend = false compiles and try_pull works.
struct chan_config_no_suspend : tmc::qu_mpsc_bounded_default_config {
  static inline constexpr bool ConsumerCanSuspend = false;
};

TEST_F(CATEGORY, try_pull_no_suspend) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, chan_config_no_suspend>{TEST_CAPACITY};

    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    static constexpr size_t NITEMS = 10;
    for (size_t i = 0; i < NITEMS; ++i) {
      bool ok = co_await chan.push(i);
      EXPECT_TRUE(ok);
    }

    size_t sum = 0;
    size_t count = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(NITEMS, count);
    size_t expectedSum = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, sum);

    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }
    co_return;
  }());
}

// Stress test: many producers, small capacity, consumer drains the queue.
// Forces heavy contention on the slot-claim CAS and the producer chain.
TEST_F(CATEGORY, stress_many_producers_small_capacity) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NPRODUCERS = 32;
    static constexpr size_t ITEMS_PER_PRODUCER = 100;
    static constexpr size_t TOTAL = NPRODUCERS * ITEMS_PER_PRODUCER;

    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{4};

    std::vector<tmc::task<void>> producers;
    producers.reserve(NPRODUCERS);
    for (size_t p = 0; p < NPRODUCERS; ++p) {
      producers.emplace_back([](auto& Chan, size_t Base) -> tmc::task<void> {
        for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
          bool ok = co_await Chan.push(Base + i);
          EXPECT_TRUE(ok);
        }
        co_return;
      }(chan, p * ITEMS_PER_PRODUCER));
    }

    auto results = co_await tmc::spawn_tuple(
      tmc::spawn_many(producers.data(), producers.size()),
      [](auto& Chan) -> tmc::task<std::pair<size_t, size_t>> {
        size_t sum = 0;
        size_t count = 0;
        for (size_t i = 0; i < TOTAL; ++i) {
          auto v = co_await Chan.pull();
          sum += *v;
          ++count;
        }
        co_return std::pair<size_t, size_t>{count, sum};
      }(chan)
    );

    auto& cons = std::get<1>(results);
    EXPECT_EQ(TOTAL, cons.first);
    size_t expectedSum = 0;
    for (size_t i = 0; i < TOTAL; ++i) {
      expectedSum += i;
    }
    EXPECT_EQ(expectedSum, cons.second);
  }());
}

// Producer push that races with close: some pushes may see CLOSED.
TEST_F(CATEGORY, push_race_with_close) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NPRODUCERS = 4;
    static constexpr size_t ITEMS_PER_PRODUCER = 200;

    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{16};

    std::atomic<size_t> total_enqueued{0};

    std::vector<tmc::task<void>> producers;
    producers.reserve(NPRODUCERS);
    for (size_t p = 0; p < NPRODUCERS; ++p) {
      producers.emplace_back(
        [](auto& Chan, size_t Base, std::atomic<size_t>& Total) -> tmc::task<void> {
          for (size_t i = 0; i < ITEMS_PER_PRODUCER; ++i) {
            bool ok = co_await Chan.push(Base + i);
            if (ok) {
              ++Total;
            }
          }
          co_return;
        }(chan, p * ITEMS_PER_PRODUCER, total_enqueued)
      );
    }

    auto results = co_await tmc::spawn_tuple(
      tmc::spawn_many(producers.data(), producers.size()),
      [](auto& Chan) -> tmc::task<size_t> {
        // Drain a bit, then close mid-stream.
        size_t drained = 0;
        for (size_t i = 0; i < 100; ++i) {
          auto v = co_await Chan.pull();
          if (!v) {
            co_return drained;
          }
          ++drained;
        }
        // Close: some in-flight pushes will see CLOSED and return false.
        Chan.close();
        // Drain the rest.
        while (true) {
          auto v = co_await Chan.pull();
          if (!v) {
            co_return drained;
          }
          ++drained;
        }
      }(chan)
    );

    size_t drained = std::get<1>(results);
    EXPECT_EQ(drained, total_enqueued.load());
  }());
}

// empty() reflects the consumer's current view.
TEST_F(CATEGORY, empty_method) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};
    EXPECT_TRUE(chan.empty());

    bool ok = co_await chan.push(size_t{7});
    EXPECT_TRUE(ok);
    EXPECT_FALSE(chan.empty());

    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(7u, *v);
    }
    EXPECT_TRUE(chan.empty());

    // After close(), the drained queue reports non-empty so the consumer will
    // pull and observe the CLOSED status.
    chan.close();
    EXPECT_FALSE(chan.empty());
    co_return;
  }());
}

TEST_F(CATEGORY, empty_when_drained) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_mpsc_bounded<size_t, qu_config<0>>{TEST_CAPACITY};

    bool ok = co_await chan.push(size_t{7});
    EXPECT_TRUE(ok);
    EXPECT_FALSE(chan.empty());

    chan.close();
    EXPECT_FALSE(chan.empty());

    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(7u, *v);
    }
    // closed-and-drained == non-empty
    EXPECT_FALSE(chan.empty());
    co_return;
  }());
}

} // namespace

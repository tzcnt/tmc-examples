#include "test_common.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/qu_bounded_spsc.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <ranges>
#include <vector>

#define CATEGORY test_qu_bounded_spsc

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static constexpr size_t SPSC_TEST_SENTINEL = static_cast<size_t>(-1);

// Default capacity used by tests below. Must be large enough to hold the
// largest bulk post in any single call (post_bulk requires Count <= capacity).
static constexpr size_t TEST_CAPACITY = 128;

template <size_t Pack> struct qu_config : tmc::qu_bounded_spsc_default_config {
  static inline constexpr size_t PackingLevel = Pack;
  static inline constexpr bool ConsumerCanSuspend = true;
};

// This version has to be default constructible
struct spsc_destructor_counter {
  std::atomic<size_t>* count;
  spsc_destructor_counter() noexcept : count{nullptr} {}
  spsc_destructor_counter(std::atomic<size_t>* C) noexcept : count{C} {}
  spsc_destructor_counter(spsc_destructor_counter const& Other) = delete;
  spsc_destructor_counter&
  operator=(spsc_destructor_counter const& Other) = delete;

  spsc_destructor_counter(spsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
  }
  spsc_destructor_counter& operator=(spsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
    return *this;
  }

  ~spsc_destructor_counter() {
    if (count != nullptr) {
      ++(*count);
    }
  }
};

// multiple tests in one to leverage the configuration options in one place
template <size_t PackingLevel, typename Executor>
void do_chan_test(Executor& Exec) {
  test_async_main(Exec, []() -> tmc::task<void> {
    {
      // general test - single push
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto chan =
        tmc::qu_bounded_spsc<size_t, qu_config<PackingLevel>>{TEST_CAPACITY};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; ++i) {
            co_await Chan.post(i);
          }
          co_await Chan.post(SPSC_TEST_SENTINEL);
          co_return i;
        }(chan),
        [](auto& Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          while (true) {
            auto v = co_await Chan.pull();
            if (*v == SPSC_TEST_SENTINEL) {
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
      // general test - post_bulk
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto chan =
        tmc::qu_bounded_spsc<size_t, qu_config<PackingLevel>>{TEST_CAPACITY};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; i += (NITEMS / 10)) {
            size_t j = i + (NITEMS / 10);
            if (j > NITEMS) {
              j = NITEMS;
            }
            co_await Chan.post_bulk(std::ranges::views::iota(i).begin(), j - i);
          }
          co_return i;
        }(chan),
        [](auto& Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          for (size_t i = 0; i < NITEMS; ++i) {
            auto v = Chan.try_pull();
            while (!v) {
              TMC_CPU_PAUSE();
              v = Chan.try_pull();
            }
            ++count;
            sum += *v;
          }
          co_return result{count, sum};
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
      // destroy chan with data remaining inside
      std::atomic<size_t> count;
      {
        auto chan = tmc::qu_bounded_spsc<
          spsc_destructor_counter, qu_config<PackingLevel>>{TEST_CAPACITY};
        for (size_t i = 0; i < 12; ++i) {
          co_await chan.post(spsc_destructor_counter{&count});
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
  do_chan_test<0>(ex());
  do_chan_test<1>(ex());
}

// Test post_bulk of 0 items
TEST_F(CATEGORY, post_bulk_none) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};
    size_t i = 0;
    for (; i < 4; ++i) {
      co_await chan.post_bulk(&i, 0);
      co_await chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      co_await chan.post(i);
    }
    for (; i < 8; ++i) {
      co_await chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      co_await chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      co_await chan.post_bulk(std::ranges::views::iota(i).begin(), 1);
    }
    size_t count = 0;
    size_t sum = 0;
    for (size_t j = 0; j < i; ++j) {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(28, sum);
    EXPECT_EQ(8, count);

    auto v = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

struct chan_config_no_suspend : tmc::qu_bounded_spsc_default_config {
  static inline constexpr bool ConsumerCanSuspend = false;
};

// Verify that try_pull works when ConsumerCanSuspend = false (pull() is
// disabled in this configuration).
TEST_F(CATEGORY, try_pull_no_suspend) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan =
      tmc::qu_bounded_spsc<size_t, chan_config_no_suspend>{TEST_CAPACITY};

    // Empty queue: try_pull yields an empty scope.
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    // Post enough items to exercise the queue.
    static constexpr size_t NITEMS = 10;
    for (size_t i = 0; i < NITEMS; ++i) {
      co_await chan.post(i);
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

    // Queue is drained: try_pull again yields an empty scope.
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }
    co_return;
  }());
}

// A type with no default, copy, or move constructor. Can only be created
// in-place via post()'s emplace forwarding.
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
    auto chan = tmc::qu_bounded_spsc<non_movable, qu_config<0>>{TEST_CAPACITY};

    // try_pull on empty queue
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    // Emplace-construct values directly in the queue storage.
    co_await chan.post(1, 2);
    co_await chan.post(3, 4);
    co_await chan.post(5, 6);

    // First value via try_pull
    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(3, v->value);
    }

    // Second value via co_await pull()
    {
      auto v = co_await chan.pull();
      EXPECT_EQ(7, v->value);
    }

    // Third value via try_pull
    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(11, v->value);
    }

    // Queue is drained
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
    using qerr = tmc::qu_bounded_spsc_err;
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};
    chan.close();

    auto v = chan.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close() is idempotent: a second call must be a no-op.
TEST_F(CATEGORY, close_idempotent) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};
    chan.close();
    chan.close();
    chan.close();
    co_return;
  }());
}

// pull() after close() returns an empty scope. This exercises the
// aw_pull::await_suspend branch where try_wait observes CLOSED_BIT.
TEST_F(CATEGORY, pull_after_close_returns_empty) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};
    chan.close();

    auto v = co_await chan.pull();
    EXPECT_FALSE(static_cast<bool>(v));

    // A second pull() should also yield an empty scope.
    auto v2 = co_await chan.pull();
    EXPECT_FALSE(static_cast<bool>(v2));
    co_return;
  }());
}

// Move-assignment of try_pull_zc_scope when the destination already holds a
// value. Two distinct queues are used because the API requires that at most
// one scope from a given queue be live at a time. The destination scope's
// element must be released (destroyed + slot freed) before adopting the
// source's element.
TEST_F(CATEGORY, try_pull_zc_scope_move_assign_over_nonempty) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::atomic<size_t> count1{0};
    std::atomic<size_t> count2{0};
    {
      auto q1 = tmc::qu_bounded_spsc<spsc_destructor_counter, qu_config<0>>{
        TEST_CAPACITY
      };
      auto q2 = tmc::qu_bounded_spsc<spsc_destructor_counter, qu_config<0>>{
        TEST_CAPACITY
      };
      co_await q1.post(spsc_destructor_counter{&count1});
      co_await q2.post(spsc_destructor_counter{&count2});

      auto v1 = q1.try_pull();
      EXPECT_TRUE(static_cast<bool>(v1));
      auto v2 = q2.try_pull();
      EXPECT_TRUE(static_cast<bool>(v2));
      EXPECT_EQ(0u, count1.load());
      EXPECT_EQ(0u, count2.load());

      // Move-assign over a non-empty scope. v1's existing element (from q1)
      // must be destroyed and its slot released; v1 then takes ownership of
      // v2's element (from q2).
      v1 = std::move(v2);
      EXPECT_FALSE(static_cast<bool>(v2));
      EXPECT_TRUE(static_cast<bool>(v1));
      EXPECT_EQ(1u, count1.load()); // q1's slot was destroyed by the assign
      EXPECT_EQ(0u, count2.load()); // q2's element now lives in v1
    } // ~v1 destroys q2's element (count2 -> 1); both queues then destruct.
    EXPECT_EQ(1u, count1.load());
    EXPECT_EQ(1u, count2.load());
    co_return;
  }());
}

// close() wakes a consumer suspended on an empty slot. The consumer is parked
// at slot 0 (no producer has posted), then close() races in and publishes the
// CLOSED sentinel at slot 0, returning the waiting consumer pointer which
// close() then resumes.
TEST_F(CATEGORY, close_wakes_suspended_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<bool> {
        auto v = co_await Chan.pull();
        co_return static_cast<bool>(v);
      }(chan),
      [](auto& Chan) -> tmc::task<void> {
        // Yield to give the consumer a chance to suspend.
        for (size_t i = 0; i < 10; ++i) {
          co_await tmc::reschedule();
        }
        Chan.close();
        co_return;
      }(chan)
    );

    bool got_value = std::get<0>(results);
    EXPECT_FALSE(got_value); // consumer was woken by close with empty scope
    co_return;
  }());
}

// post_bulk() wakes a consumer suspended on the slot it writes. Exercises
// the branch in post_bulk that captures the waiting consumer pointer
// returned by write_element and posts its resumption.
TEST_F(CATEGORY, post_bulk_wakes_suspended_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<size_t> {
        // Consumer suspends at slot 0; post_bulk wakes it. Drain everything
        // until the queue is closed (empty scope). Each pull_zc_scope must
        // be released before the next pull() runs, since the queue requires
        // that at most one scope from a given queue be live at a time.
        size_t sum = 0;
        while (auto v = co_await Chan.pull()) {
          sum += *v;
        }
        co_return sum;
      }(chan),
      [](auto& Chan) -> tmc::task<void> {
        // Yield to give the consumer a chance to suspend.
        for (size_t i = 0; i < 10; ++i) {
          co_await tmc::reschedule();
        }
        size_t items[5] = {10, 20, 30, 40, 50};
        co_await Chan.post_bulk(&items[0], 5);
        Chan.close();
        co_return;
      }(chan)
    );

    EXPECT_EQ(10u + 20u + 30u + 40u + 50u, std::get<0>(results));
    co_return;
  }());
}

// close_resume_inline() with no waiting consumer: behaves like close() in
// that subsequent posts must not be used, and pre-close posts can still drain.
TEST_F(CATEGORY, close_resume_inline_no_waiting_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_bounded_spsc_err;
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    for (size_t i = 0; i < 5; ++i) {
      co_await chan.post(i);
    }

    chan.close_resume_inline();

    // Drain the queue.
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
        ADD_FAILURE() << "Unexpected EMPTY after close_resume_inline";
        break;
      }
    }
    EXPECT_EQ(5u, count);
    EXPECT_EQ(0u + 1u + 2u + 3u + 4u, sum);

    // Further try_pull stays CLOSED.
    auto v = chan.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    EXPECT_FALSE(static_cast<bool>(v));

    // close_resume_inline() is idempotent.
    chan.close_resume_inline();
    chan.close();
    co_return;
  }());
}

// close_resume_inline() wakes a consumer suspended on an empty slot, just like
// close() does. The consumer is parked at slot 0 (no producer has posted),
// then close_resume_inline() races in and publishes the CLOSED sentinel at slot
// 0, which wakes the consumer (resumed inline) with an empty scope.
TEST_F(CATEGORY, close_resume_inline_wakes_suspended_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<bool> {
        auto v = co_await Chan.pull();
        co_return static_cast<bool>(v);
      }(chan),
      [](auto& Chan) -> tmc::task<void> {
        // Yield to give the consumer a chance to suspend.
        for (size_t i = 0; i < 10; ++i) {
          co_await tmc::reschedule();
        }
        Chan.close_resume_inline();
        co_return;
      }(chan)
    );

    bool got_value = std::get<0>(results);
    EXPECT_FALSE(
      got_value
    ); // consumer was woken by close_resume_inline with empty scope
    co_return;
  }());
}

TEST_F(CATEGORY, pull_after_closed) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_bounded_spsc_err;
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<void> {
        // Suspend on the empty cutoff slot.
        auto v = co_await Chan.pull();
        EXPECT_FALSE(static_cast<bool>(v)); // woken by close, empty.

        // Now poll the same slot. Per the close() contract this must
        // immediately return CLOSED again.
        auto v2 = Chan.try_pull();
        EXPECT_EQ(qerr::CLOSED, v2.status());
        EXPECT_EQ(false, v2.has_value());
        auto v3 = co_await Chan.pull();
        EXPECT_EQ(false, v3.has_value());
      }(chan),
      [](auto& Chan) -> tmc::task<void> {
        // Yield to give the consumer a chance to suspend.
        for (size_t i = 0; i < 10; ++i) {
          co_await tmc::reschedule();
        }
        Chan.close();
      }(chan)
    );

    co_return;
  }());
}

// post_bulk(Begin, End) iterator-pair overload: pushes [Begin, End) into the
// queue and drains them in order.
TEST_F(CATEGORY, post_bulk_iterator_pair) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    std::vector<size_t> items{1, 2, 3, 4, 5, 6, 7};
    co_await chan.post_bulk(items.begin(), items.end());

    size_t sum = 0;
    size_t count = 0;
    for (size_t i = 0; i < items.size(); ++i) {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(items.size(), count);
    EXPECT_EQ(1u + 2u + 3u + 4u + 5u + 6u + 7u, sum);

    // Queue is drained.
    auto empty = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(empty));
    co_return;
  }());
}

// post_bulk(Begin, End) with Begin == End must be a no-op.
TEST_F(CATEGORY, post_bulk_iterator_pair_empty) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    std::vector<size_t> empty_items;
    co_await chan.post_bulk(empty_items.begin(), empty_items.end());

    // Queue should still be empty.
    auto v = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));

    // A real post afterwards should still work and arrive at slot 0.
    co_await chan.post(42u);
    auto v2 = chan.try_pull();
    EXPECT_TRUE(static_cast<bool>(v2));
    EXPECT_EQ(42u, *v2);
    co_return;
  }());
}

// post_bulk(Range) overload: pushes the range into the queue and drains in
// order.
TEST_F(CATEGORY, post_bulk_range) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    std::vector<size_t> items{10, 20, 30, 40, 50};
    co_await chan.post_bulk(items);

    size_t sum = 0;
    size_t count = 0;
    for (size_t i = 0; i < items.size(); ++i) {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(items.size(), count);
    EXPECT_EQ(10u + 20u + 30u + 40u + 50u, sum);

    // Queue is drained.
    auto empty = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(empty));
    co_return;
  }());
}

// post() wakes a consumer suspended on the slot it writes. Exercises the
// branch in aw_post_impl::await_resume that captures the waiting consumer
// pointer returned by write_element and posts its resumption.
TEST_F(CATEGORY, post_wakes_suspended_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<size_t> {
        size_t sum = 0;
        while (auto v = co_await Chan.pull()) {
          sum += *v;
        }
        co_return sum;
      }(chan),
      [](auto& Chan) -> tmc::task<void> {
        // Yield to give the consumer a chance to suspend.
        for (size_t i = 0; i < 10; ++i) {
          co_await tmc::reschedule();
        }
        co_await Chan.post(123u);
        co_await Chan.post(456u);
        Chan.close();
        co_return;
      }(chan)
    );

    EXPECT_EQ(123u + 456u, std::get<0>(results));
    co_return;
  }());
}

// Fill the queue to capacity, then have the producer attempt one more post()
// which must suspend until the consumer drains. Exercises
// aw_post_impl::await_suspend (producer suspension on a DATA_BIT slot) and
// finish_read's branch that wakes a producer when prev flags is a
// producer_base*.
TEST_F(CATEGORY, post_suspends_when_full) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Tiny capacity so we can easily fill the queue.
    static constexpr size_t CAP = 4;
    static constexpr size_t NITEMS = 32;
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{CAP};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<size_t> {
        size_t sum = 0;
        for (size_t i = 0; i < NITEMS; ++i) {
          // Each post may suspend once the queue fills up.
          co_await Chan.post(i);
          sum += i;
        }
        co_return sum;
      }(chan),
      [](auto& Chan) -> tmc::task<size_t> {
        // Give the producer time to fill the queue and suspend.
        for (size_t i = 0; i < 20; ++i) {
          co_await tmc::reschedule();
        }
        size_t sum = 0;
        for (size_t i = 0; i < NITEMS; ++i) {
          auto v = co_await Chan.pull();
          EXPECT_TRUE(static_cast<bool>(v));
          sum += *v;
        }
        co_return sum;
      }(chan)
    );

    size_t expected = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expected += i;
    }
    EXPECT_EQ(expected, std::get<0>(results));
    EXPECT_EQ(expected, std::get<1>(results));
    co_return;
  }());
}

// Fill the queue to capacity, then have the producer attempt a post_bulk()
// that does not fit. Exercises aw_post_bulk_impl::await_suspend (producer
// suspension on a full bulk slot) and the finish_read wake-producer branch.
TEST_F(CATEGORY, post_bulk_suspends_when_full) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t CAP = 4;
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{CAP};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<size_t> {
        size_t items[CAP] = {1, 2, 3, 4};
        // First fill the queue exactly.
        co_await Chan.post_bulk(&items[0], CAP);
        // This second post_bulk cannot fit any element; producer must
        // suspend until the consumer drains the queue.
        size_t more[CAP] = {10, 20, 30, 40};
        co_await Chan.post_bulk(&more[0], CAP);
        co_return 1u + 2u + 3u + 4u + 10u + 20u + 30u + 40u;
      }(chan),
      [](auto& Chan) -> tmc::task<size_t> {
        // Give the producer time to fill and then suspend on post_bulk.
        for (size_t i = 0; i < 20; ++i) {
          co_await tmc::reschedule();
        }
        size_t sum = 0;
        for (size_t i = 0; i < 2 * CAP; ++i) {
          auto v = co_await Chan.pull();
          EXPECT_TRUE(static_cast<bool>(v));
          sum += *v;
        }
        co_return sum;
      }(chan)
    );

    EXPECT_EQ(std::get<0>(results), std::get<1>(results));
    co_return;
  }());
}

// empty() returns true on an empty queue and false after a post; returns true
// again after a try_pull drains the slot. Exercises empty() and the
// is_data_waiting() helper.
TEST_F(CATEGORY, empty_method) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};
    EXPECT_TRUE(chan.empty());

    co_await chan.post(7u);
    EXPECT_FALSE(chan.empty());

    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(7u, *v);
    }
    EXPECT_TRUE(chan.empty());

    // Bulk fill, then drain.
    size_t items[3] = {100, 200, 300};
    co_await chan.post_bulk(&items[0], 3);
    EXPECT_FALSE(chan.empty());
    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
    }
    EXPECT_FALSE(chan.empty());
    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
    }
    EXPECT_FALSE(chan.empty());
    {
      auto v = chan.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
    }
    EXPECT_TRUE(chan.empty());
    co_return;
  }());
}

// post_bulk(Range) with an empty range must be a no-op.
TEST_F(CATEGORY, post_bulk_range_empty) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_bounded_spsc<size_t, qu_config<0>>{TEST_CAPACITY};

    std::vector<size_t> empty_items;
    co_await chan.post_bulk(empty_items);

    // Queue should still be empty.
    auto v = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));

    // A real post afterwards should still work and arrive at slot 0.
    co_await chan.post(99u);
    auto v2 = chan.try_pull();
    EXPECT_TRUE(static_cast<bool>(v2));
    EXPECT_EQ(99u, *v2);
    co_return;
  }());
}

#undef CATEGORY

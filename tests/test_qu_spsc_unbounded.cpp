#include "test_common.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/ex_cpu_st.hpp"
#include "tmc/qu_spsc_unbounded.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <ranges>
#include <vector>

#define CATEGORY test_qu_spsc_unbounded

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static constexpr size_t SPSC_TEST_SENTINEL = static_cast<size_t>(-1);

template <size_t Pack>
struct qu_config : tmc::qu_spsc_unbounded_default_config {
  // Use a small block size to ensure that alloc / reclaim is triggered.
  static inline constexpr size_t BlockSize = 2;
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

      auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<PackingLevel>>{};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; ++i) {
            Chan.post(i);
          }
          Chan.post(SPSC_TEST_SENTINEL);
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

      auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<PackingLevel>>{};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Chan) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; i += (NITEMS / 10)) {
            size_t j = i + (NITEMS / 10);
            if (j > NITEMS) {
              j = NITEMS;
            }
            Chan.post_bulk(std::ranges::views::iota(i).begin(), j - i);
          }
          co_return i;
        }(chan),
        [](auto& Chan) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          for (size_t i = 0; i < NITEMS; ++i) {
            auto v = Chan.try_pull();
            while (!v) {
              co_await tmc::reschedule();
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
      std::atomic<size_t> count{0};
      {
        auto chan = tmc::qu_spsc_unbounded<
          spsc_destructor_counter, qu_config<PackingLevel>>{};
        for (size_t i = 0; i < 12; ++i) {
          chan.post(spsc_destructor_counter{&count});
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
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};
    size_t i = 0;
    for (; i < 4; ++i) {
      chan.post_bulk(&i, 0);
      chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      chan.post(i);
    }
    for (; i < 8; ++i) {
      chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      chan.post_bulk(std::ranges::views::iota(i).begin(), 0);
      chan.post_bulk(std::ranges::views::iota(i).begin(), 1);
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

struct chan_config_no_suspend : tmc::qu_spsc_unbounded_default_config {
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr bool ConsumerCanSuspend = false;
};

// Verify that try_pull works when ConsumerCanSuspend = false (pull() is
// disabled in this configuration).
TEST_F(CATEGORY, try_pull_no_suspend) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_spsc_unbounded<size_t, chan_config_no_suspend>{};

    // Empty queue: try_pull yields an empty scope.
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    // Post enough items to cross several blocks.
    static constexpr size_t NITEMS = 10;
    for (size_t i = 0; i < NITEMS; ++i) {
      chan.post(i);
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

// BlockSize == 1 is the minimum allowed block size. Every element lives in
// its own block, so every consumed element is a block-start and triggers the
// block reclaim path in finish_read.
struct chan_config_block1 : tmc::qu_spsc_unbounded_default_config {
  static inline constexpr size_t BlockSize = 1;
};

TEST_F(CATEGORY, block_size_one) {
  // Single-threaded: post then drain, exercising reclaim on every pull.
  {
    tmc::ex_cpu ex1;
    ex1.set_thread_count(1).init();
    test_async_main(ex1, []() -> tmc::task<void> {
      auto chan = tmc::qu_spsc_unbounded<size_t, chan_config_block1>{};

      static constexpr size_t NITEMS = 10;
      for (size_t i = 0; i < NITEMS; ++i) {
        chan.post(i);
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

      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
      co_return;
    }());
  }
  // Concurrent producer and consumer racing block alloc / reclaim.
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NITEMS = 1000;
    struct result {
      size_t count;
      size_t sum;
    };

    auto chan = tmc::qu_spsc_unbounded<size_t, chan_config_block1>{};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Chan) -> tmc::task<size_t> {
        size_t i = 0;
        for (; i < NITEMS; ++i) {
          Chan.post(i);
        }
        Chan.post(SPSC_TEST_SENTINEL);
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
    auto chan = tmc::qu_spsc_unbounded<non_movable, qu_config<0>>{};

    // try_pull on empty queue
    {
      auto v = chan.try_pull();
      EXPECT_FALSE(static_cast<bool>(v));
    }

    // Emplace-construct values directly in the queue storage.
    chan.post(1, 2);
    chan.post(3, 4);
    chan.post(5, 6);

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
    using qerr = tmc::qu_spsc_unbounded_err;
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};
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
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};
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
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};
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
      auto q1 = tmc::qu_spsc_unbounded<spsc_destructor_counter, qu_config<0>>{};
      auto q2 = tmc::qu_spsc_unbounded<spsc_destructor_counter, qu_config<0>>{};
      q1.post(spsc_destructor_counter{&count1});
      q2.post(spsc_destructor_counter{&count2});

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
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    auto consumer = tmc::spawn([](auto& Chan) -> tmc::task<bool> {
                      auto v = co_await Chan.pull();
                      co_return static_cast<bool>(v);
                    }(chan))
                      .fork();

    // Force the consumer to run first and suspend on the empty queue.
    co_await tmc::reschedule();
    chan.close();

    bool got_value = co_await std::move(consumer);
    EXPECT_FALSE(got_value); // consumer was woken by close with empty scope
    co_return;
  }());
}

// post_bulk() wakes a consumer suspended on the slot it writes. Exercises
// the branch in post_bulk that captures the waiting consumer pointer
// returned by write_element and posts its resumption.
TEST_F(CATEGORY, post_bulk_wakes_suspended_consumer) {
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    auto consumer = tmc::spawn([](auto& Chan) -> tmc::task<size_t> {
                      // Consumer suspends at slot 0; post_bulk wakes it.
                      // Drain everything until the queue is closed (empty
                      // scope). Each pull_zc_scope must be released before
                      // the next pull() runs, since the queue requires that
                      // at most one scope from a given queue be live at a
                      // time.
                      size_t sum = 0;
                      while (auto v = co_await Chan.pull()) {
                        sum += *v;
                      }
                      co_return sum;
                    }(chan))
                      .fork();

    // Force the consumer to run first and suspend on the empty queue.
    co_await tmc::reschedule();
    size_t items[5] = {10, 20, 30, 40, 50};
    chan.post_bulk(&items[0], 5);
    chan.close();

    auto sum = co_await std::move(consumer);
    EXPECT_EQ(10u + 20u + 30u + 40u + 50u, sum);
    co_return;
  }());
}

// close_resume_inline() with no waiting consumer: behaves like close() in
// that subsequent posts must not be used, and pre-close posts can still drain.
TEST_F(CATEGORY, close_resume_inline_no_waiting_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_spsc_unbounded_err;
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    for (size_t i = 0; i < 5; ++i) {
      chan.post(i);
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
  tmc::ex_cpu_st exec;
  exec.init();
  test_async_main(exec, []() -> tmc::task<void> {
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    auto consumer = tmc::spawn([](auto& Chan) -> tmc::task<bool> {
                      auto v = co_await Chan.pull();
                      co_return static_cast<bool>(v);
                    }(chan))
                      .fork();

    // Force the consumer to run first and suspend on the empty queue.
    co_await tmc::reschedule();
    chan.close_resume_inline();

    bool got_value = co_await std::move(consumer);
    EXPECT_FALSE(
      got_value
    ); // consumer was woken by close_resume_inline with empty scope
    co_return;
  }());
}

// post_bulk(Begin, End) iterator-pair overload: pushes [Begin, End) into the
// queue and drains them in order.
TEST_F(CATEGORY, post_bulk_iterator_pair) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    std::vector<size_t> items{1, 2, 3, 4, 5, 6, 7};
    chan.post_bulk(items.begin(), items.end());

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
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    std::vector<size_t> empty_items;
    chan.post_bulk(empty_items.begin(), empty_items.end());

    // Queue should still be empty.
    auto v = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));

    // A real post afterwards should still work and arrive at slot 0.
    chan.post(42u);
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
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    std::vector<size_t> items{10, 20, 30, 40, 50};
    chan.post_bulk(items);

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

// post_bulk(Range) with an empty range must be a no-op.
TEST_F(CATEGORY, post_bulk_range_empty) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto chan = tmc::qu_spsc_unbounded<size_t, qu_config<0>>{};

    std::vector<size_t> empty_items;
    chan.post_bulk(empty_items);

    // Queue should still be empty.
    auto v = chan.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));

    // A real post afterwards should still work and arrive at slot 0.
    chan.post(99u);
    auto v2 = chan.try_pull();
    EXPECT_TRUE(static_cast<bool>(v2));
    EXPECT_EQ(99u, *v2);
    co_return;
  }());
}

#undef CATEGORY

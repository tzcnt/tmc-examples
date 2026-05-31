#include "test_common.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/qu_mpsc_unbounded.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <ranges>

#define CATEGORY test_qu_mpsc_unbounded

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::cpu_executor().init(); }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static constexpr size_t MPSC_TEST_SENTINEL = static_cast<size_t>(-1);

template <size_t Pack> struct q_config : tmc::qu_mpsc_unbounded_default_config {
  // Use a small block size to ensure that alloc / reclaim is triggered.
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr size_t PackingLevel = Pack;
  static inline constexpr bool ConsumerCanSuspend = true;
};

// This version has to be default constructible
struct mpsc_destructor_counter {
  std::atomic<size_t>* count;
  mpsc_destructor_counter() noexcept : count{nullptr} {}
  mpsc_destructor_counter(std::atomic<size_t>* C) noexcept : count{C} {}
  mpsc_destructor_counter(mpsc_destructor_counter const& Other) = delete;
  mpsc_destructor_counter&
  operator=(mpsc_destructor_counter const& Other) = delete;

  mpsc_destructor_counter(mpsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
  }
  mpsc_destructor_counter& operator=(mpsc_destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
    return *this;
  }

  ~mpsc_destructor_counter() {
    if (count != nullptr) {
      ++(*count);
    }
  }
};

// multiple tests in one to leverage the configuration options in one place
template <size_t PackingLevel, typename Executor>
void do_q_test(Executor& Exec) {
  test_async_main(Exec, []() -> tmc::task<void> {
    {
      // general test - single push
      static constexpr size_t NITEMS = 1000;
      struct result {
        size_t count;
        size_t sum;
      };

      auto q = tmc::qu_mpsc_unbounded<size_t, q_config<PackingLevel>>{};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Q) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; ++i) {
            Q.post(i);
          }
          Q.post(MPSC_TEST_SENTINEL);
          co_return i;
        }(q),
        [](auto& Q) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          while (true) {
            auto v = co_await Q.pull();
            if (*v == MPSC_TEST_SENTINEL) {
              co_return result{count, sum};
            }
            ++count;
            sum += *v;
          }
        }(q)
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

      auto q = tmc::qu_mpsc_unbounded<size_t, q_config<PackingLevel>>{};

      auto results = co_await tmc::spawn_tuple(
        [](auto& Q) -> tmc::task<size_t> {
          size_t i = 0;
          for (; i < NITEMS; i += (NITEMS / 10)) {
            size_t j = i + (NITEMS / 10);
            if (j > NITEMS) {
              j = NITEMS;
            }
            Q.post_bulk(std::ranges::views::iota(i).begin(), j - i);
          }
          co_return i;
        }(q),
        [](auto& Q) -> tmc::task<result> {
          size_t count = 0;
          size_t sum = 0;
          for (size_t i = 0; i < NITEMS; ++i) {
            auto v = Q.try_pull();
            while (!v) {
              TMC_CPU_PAUSE();
              v = Q.try_pull();
            }
            ++count;
            sum += *v;
          }
          co_return result{count, sum};
        }(q)
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
      // destroy q with data remaining inside
      std::atomic<size_t> count;
      {
        auto q = tmc::qu_mpsc_unbounded<
          mpsc_destructor_counter, q_config<PackingLevel>>{};
        for (size_t i = 0; i < 12; ++i) {
          q.post(mpsc_destructor_counter{&count});
        }

        for (size_t i = 0; i < 7; ++i) {
          auto ok = q.try_pull();
          EXPECT_TRUE(static_cast<bool>(ok));
        }

        EXPECT_EQ(count.load(), 7);
      }
      // Now q goes out of scope; remaining data's destructors are called
      EXPECT_EQ(count.load(), 12);
    }
  }());
}

TEST_F(CATEGORY, config_sweep) {
  do_q_test<0>(ex());
  do_q_test<1>(ex());
}

// Test post_bulk of 0 items
TEST_F(CATEGORY, post_bulk_none) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  test_async_main(ex, []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    size_t i = 0;
    for (; i < 4; ++i) {
      q.post_bulk(&i, 0);
      q.post_bulk(std::ranges::views::iota(i).begin(), 0);
      q.post(i);
    }
    for (; i < 8; ++i) {
      q.post_bulk(std::ranges::views::iota(i).begin(), 0);
      q.post_bulk(std::ranges::views::iota(i).begin(), 0);
      q.post_bulk(std::ranges::views::iota(i).begin(), 1);
    }
    size_t count = 0;
    size_t sum = 0;
    for (size_t j = 0; j < i; ++j) {
      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(28, sum);
    EXPECT_EQ(8, count);

    auto v = q.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close() makes subsequent post() return false. Pre-close posts still drain.
TEST_F(CATEGORY, close_basic_try_pull) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_unbounded_err;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    for (size_t i = 0; i < 5; ++i) {
      EXPECT_TRUE(q.post(i));
    }

    q.close();

    // post() after close should fail.
    EXPECT_FALSE(q.post(static_cast<size_t>(99)));

    // Drain the queue.
    size_t sum = 0;
    size_t count = 0;
    while (true) {
      auto v = q.try_pull();
      if (v.status() == qerr::OK) {
        sum += *v;
        ++count;
      } else if (v.status() == qerr::CLOSED) {
        break;
      } else {
        // Spin on EMPTY (shouldn't happen here since producers all finished).
        ADD_FAILURE() << "Unexpected EMPTY after close";
        break;
      }
    }
    EXPECT_EQ(5u, count);
    EXPECT_EQ(0u + 1u + 2u + 3u + 4u, sum);

    // Further try_pull stays CLOSED.
    auto v = q.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close() with no values posted: try_pull immediately returns CLOSED.
TEST_F(CATEGORY, close_empty_try_pull) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_unbounded_err;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    q.close();

    auto v = q.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close() is idempotent.
TEST_F(CATEGORY, close_idempotent) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    q.close();
    q.close();
    q.close();
    EXPECT_FALSE(q.post(static_cast<size_t>(1)));
    co_return;
  }());
}

// pull() resumes with an empty scope when the queue is closed and drained.
TEST_F(CATEGORY, close_pull_drains_then_returns_empty) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    for (size_t i = 0; i < 3; ++i) {
      EXPECT_TRUE(q.post(i));
    }
    q.close();

    // Drain via pull().
    size_t count = 0;
    size_t sum = 0;
    while (true) {
      auto v = co_await q.pull();
      if (!v) {
        break; // closed and drained
      }
      sum += *v;
      ++count;
    }
    EXPECT_EQ(3u, count);
    EXPECT_EQ(0u + 1u + 2u, sum);
    co_return;
  }());
}

// close() wakes a consumer suspended on an empty slot. The consumer is parked
// at slot 0 (no producer has posted), then close() races in and publishes the
// CLOSED sentinel at slot 0, which wakes the consumer with an empty scope.
TEST_F(CATEGORY, close_wakes_suspended_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Q) -> tmc::task<bool> {
        auto v = co_await Q.pull();
        co_return static_cast<bool>(v);
      }(q),
      [](auto& Q) -> tmc::task<void> {
        // Yield to give the consumer a qce to suspend.
        for (size_t i = 0; i < 10; ++i) {
          co_await tmc::reschedule();
        }
        Q.close();
        co_return;
      }(q)
    );

    bool got_value = std::get<0>(results);
    EXPECT_FALSE(got_value); // consumer was woken by close with empty scope
    co_return;
  }());
}

// post_bulk() returns false when called after close.
TEST_F(CATEGORY, close_post_bulk_returns_false) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    q.close();

    size_t vals[3] = {10, 11, 12};
    EXPECT_FALSE(q.post_bulk(&vals[0], 3));
    co_return;
  }());
}

// Concurrent producer + close: every post() that returns true must be
// observed by the consumer, and once close returns, subsequent posts fail.
TEST_F(CATEGORY, close_concurrent_producer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NITEMS = 2000;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    std::atomic<size_t> posted_ok{0};

    auto results = co_await tmc::spawn_tuple(
      [&](auto& Q) -> tmc::task<void> {
        for (size_t i = 0; i < NITEMS; ++i) {
          if (Q.post(static_cast<size_t>(1))) {
            posted_ok.fetch_add(1, std::memory_order_relaxed);
          }
          // Yield occasionally to allow other tasks to run.
          if ((i & 0x3F) == 0) {
            co_await tmc::reschedule();
          }
        }
        // After this producer is done, close the queue. There are no other
        // producers, so all posts() that succeeded are pre-close.
        Q.close();
        // Subsequent posts must fail.
        EXPECT_FALSE(Q.post(static_cast<size_t>(999)));
        co_return;
      }(q),
      [](auto& Q) -> tmc::task<size_t> {
        size_t pulled = 0;
        while (true) {
          auto v = co_await Q.pull();
          if (!v) {
            co_return pulled;
          }
          pulled += *v;
        }
      }(q)
    );

    size_t pulled = std::get<1>(results);
    EXPECT_EQ(posted_ok.load(), pulled);
    EXPECT_EQ(NITEMS, pulled);
    co_return;
  }());
}

// close_resume_inline() behaves like close() for post(): subsequent posts fail
// and pre-close posts still drain.
TEST_F(CATEGORY, close_resume_inline_basic_try_pull) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_unbounded_err;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    for (size_t i = 0; i < 5; ++i) {
      EXPECT_TRUE(q.post(i));
    }

    q.close_resume_inline();

    // post() after close_resume_inline should fail.
    EXPECT_FALSE(q.post(static_cast<size_t>(99)));

    // Drain the queue.
    size_t sum = 0;
    size_t count = 0;
    while (true) {
      auto v = q.try_pull();
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
    auto v = q.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close_resume_inline() with no values posted: try_pull immediately returns
// CLOSED.
TEST_F(CATEGORY, close_resume_inline_empty_try_pull) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_unbounded_err;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    q.close_resume_inline();

    auto v = q.try_pull();
    EXPECT_EQ(qerr::CLOSED, v.status());
    EXPECT_FALSE(static_cast<bool>(v));
    co_return;
  }());
}

// close_resume_inline() is idempotent and may be intermixed with close().
TEST_F(CATEGORY, close_resume_inline_idempotent) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    q.close_resume_inline();
    q.close_resume_inline();
    q.close();
    q.close_resume_inline();
    EXPECT_FALSE(q.post(static_cast<size_t>(1)));
    co_return;
  }());
}

// pull() resumes with an empty scope when the queue is closed_inline and
// drained.
TEST_F(CATEGORY, close_resume_inline_pull_drains_then_returns_empty) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    for (size_t i = 0; i < 3; ++i) {
      EXPECT_TRUE(q.post(i));
    }
    q.close_resume_inline();

    // Drain via pull().
    size_t count = 0;
    size_t sum = 0;
    while (true) {
      auto v = co_await q.pull();
      if (!v) {
        break; // closed and drained
      }
      sum += *v;
      ++count;
    }
    EXPECT_EQ(3u, count);
    EXPECT_EQ(0u + 1u + 2u, sum);
    co_return;
  }());
}

// close_resume_inline() wakes a consumer suspended on an empty slot, just like
// close() does. The consumer is parked at slot 0 (no producer has posted),
// then close_resume_inline() races in and publishes the CLOSED sentinel at slot
// 0, which wakes the consumer with an empty scope.
TEST_F(CATEGORY, close_resume_inline_wakes_suspended_consumer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Q) -> tmc::task<bool> {
        auto v = co_await Q.pull();
        co_return static_cast<bool>(v);
      }(q),
      [](auto& Q) -> tmc::task<void> {
        // Yield to give the consumer a qce to suspend.
        for (size_t i = 0; i < 10; ++i) {
          co_await tmc::reschedule();
        }
        Q.close_resume_inline();
        co_return;
      }(q)
    );

    bool got_value = std::get<0>(results);
    EXPECT_FALSE(
      got_value
    ); // consumer was woken by close_resume_inline with empty
    co_return;
  }());
}

// Config that uses the default ConsumerCanSuspend = false. This exercises
// the non-suspending code path in write_element (set_data_ready()), which
// q_config<> overrides to true and which is otherwise never tested.
struct q_config_no_suspend : tmc::qu_mpsc_unbounded_default_config {
  static inline constexpr size_t BlockSize = 2;
  // ConsumerCanSuspend defaults to false
};

TEST_F(CATEGORY, no_suspend_try_pull_only) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_unbounded_err;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config_no_suspend>{};

    // No data available yet: try_pull returns EMPTY.
    {
      auto v = q.try_pull();
      EXPECT_EQ(qerr::EMPTY, v.status());
      EXPECT_FALSE(static_cast<bool>(v));
    }

    static constexpr size_t NITEMS = 100;
    for (size_t i = 0; i < NITEMS; ++i) {
      EXPECT_TRUE(q.post(i));
    }

    size_t sum = 0;
    size_t count = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      auto v = q.try_pull();
      EXPECT_EQ(qerr::OK, v.status());
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(NITEMS, count);
    size_t expected = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expected += i;
    }
    EXPECT_EQ(expected, sum);

    // After draining: EMPTY again.
    auto v = q.try_pull();
    EXPECT_EQ(qerr::EMPTY, v.status());
    EXPECT_FALSE(static_cast<bool>(v));

    // Close and verify CLOSED status.
    q.close();
    auto v2 = q.try_pull();
    EXPECT_EQ(qerr::CLOSED, v2.status());
    co_return;
  }());
}

// EmbedFirstBlock = true: first block is embedded in the qu_mpsc_unbounded
// object. Push and reclaim past the embedded block to ensure the destructor
// handles the embedded-vs-heap distinction correctly.
struct q_config_embed : tmc::qu_mpsc_unbounded_default_config {
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr bool EmbedFirstBlock = true;
  static inline constexpr bool ConsumerCanSuspend = true;
};

TEST_F(CATEGORY, embed_first_block) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config_embed>{};
    static constexpr size_t NITEMS = 50; // many block transitions
    size_t sum = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      EXPECT_TRUE(q.post(i));
      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
    }
    size_t expected = 0;
    for (size_t i = 0; i < NITEMS; ++i) {
      expected += i;
    }
    EXPECT_EQ(expected, sum);
    co_return;
  }());

  // Also test the destructor path when the embedded block holds undrained
  // data. This ensures we don't try to delete the embedded block.
  {
    std::atomic<size_t> count{0};
    {
      auto q =
        tmc::qu_mpsc_unbounded<mpsc_destructor_counter, q_config_embed>{};
      // 2 items fit in the embedded block (BlockSize=2). Add more to also
      // exercise heap blocks alongside the embedded block.
      for (size_t i = 0; i < 5; ++i) {
        q.post(mpsc_destructor_counter{&count});
      }
    }
    EXPECT_EQ(count.load(), 5);
  }
}

// Multiple concurrent producers. This is what the M in MPSC stands for; the
// existing tests only use a single producer task.
TEST_F(CATEGORY, multi_producer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NPRODUCERS = 4;
    static constexpr size_t PER_PRODUCER = 500;
    static constexpr size_t TOTAL = NPRODUCERS * PER_PRODUCER;

    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    auto results = co_await tmc::spawn_tuple(
      [](auto& Q) -> tmc::task<size_t> {
        // Producer orchestrator: spawn NPRODUCERS producers in parallel.
        std::vector<tmc::task<size_t>> producers;
        for (size_t p = 0; p < NPRODUCERS; ++p) {
          producers.push_back([](auto& C, size_t Base) -> tmc::task<size_t> {
            size_t produced = 0;
            for (size_t i = 0; i < PER_PRODUCER; ++i) {
              C.post(Base + i);
              ++produced;
              if ((i & 0x1F) == 0) {
                co_await tmc::reschedule();
              }
            }
            co_return produced;
          }(Q, p * PER_PRODUCER));
        }
        auto counts =
          co_await tmc::spawn_many(producers.data(), producers.size());
        size_t totalProduced = 0;
        for (auto c : counts) {
          totalProduced += c;
        }
        // Wake the consumer.
        Q.post(MPSC_TEST_SENTINEL);
        co_return totalProduced;
      }(q),
      [](auto& Q) -> tmc::task<size_t> {
        size_t pulled = 0;
        while (true) {
          auto v = co_await Q.pull();
          if (!v) {
            co_return pulled;
          }
          if (*v == MPSC_TEST_SENTINEL) {
            co_return pulled;
          }
          ++pulled;
        }
      }(q)
    );

    EXPECT_EQ(TOTAL, std::get<0>(results));
    EXPECT_EQ(TOTAL, std::get<1>(results));
    co_return;
  }());
}

// Move construction / move assignment of try_pull_zc_scope. Ensures the
// moved-from scope releases its slot exactly once and the moved-to scope
// can still read the value.
TEST_F(CATEGORY, try_pull_zc_scope_move) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Move-construct.
    {
      std::atomic<size_t> count{0};
      {
        auto q = tmc::qu_mpsc_unbounded<mpsc_destructor_counter, q_config<0>>{};
        q.post(mpsc_destructor_counter{&count});
        q.post(mpsc_destructor_counter{&count});
        q.post(mpsc_destructor_counter{&count});

        auto v1 = q.try_pull();
        EXPECT_TRUE(static_cast<bool>(v1));
        auto v2 = std::move(v1);
        EXPECT_FALSE(static_cast<bool>(v1));
        EXPECT_TRUE(static_cast<bool>(v2));
        EXPECT_NE(v2->count, nullptr);
        // v1's destructor must be a no-op; only v2's release the slot.
      } // v2 destructor releases slot 0; remaining 2 destroyed by
        // ~qu_mpsc_unbounded
      EXPECT_EQ(count.load(), 3);
    }

    // Move-assign over an empty scope.
    {
      std::atomic<size_t> count{0};
      {
        using qu = tmc::qu_mpsc_unbounded<mpsc_destructor_counter, q_config<0>>;
        qu q{};
        q.post(mpsc_destructor_counter{&count});
        q.post(mpsc_destructor_counter{&count});

        typename qu::try_pull_zc_scope dest;
        EXPECT_FALSE(static_cast<bool>(dest));
        auto src = q.try_pull();
        EXPECT_TRUE(static_cast<bool>(src));
        dest = std::move(src);
        EXPECT_FALSE(static_cast<bool>(src));
        EXPECT_TRUE(static_cast<bool>(dest));
      }
      EXPECT_EQ(count.load(), 2);
    }
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
      auto q1 = tmc::qu_mpsc_unbounded<mpsc_destructor_counter, q_config<0>>{};
      auto q2 = tmc::qu_mpsc_unbounded<mpsc_destructor_counter, q_config<0>>{};
      q1.post(mpsc_destructor_counter{&count1});
      q2.post(mpsc_destructor_counter{&count2});

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

// Self-move-assignment of try_pull_zc_scope must be a no-op: the held value
// stays valid and the slot is not double-released. (This exercises the
// `this != &Other` early-out branch in operator=.)
TEST_F(CATEGORY, try_pull_zc_scope_self_move_assign) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::atomic<size_t> count{0};
    {
      auto q = tmc::qu_mpsc_unbounded<mpsc_destructor_counter, q_config<0>>{};
      q.post(mpsc_destructor_counter{&count});

      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));

      // Self-move-assign through a reference to defeat compiler warnings.
      auto& vref = v;
      v = std::move(vref);

      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_NE(v->count, nullptr);
      EXPECT_EQ(0u, count.load());
    }
    EXPECT_EQ(1u, count.load());
    co_return;
  }());
}

// Explicit EMPTY status before close().
TEST_F(CATEGORY, try_pull_empty_status) {
  test_async_main(ex(), []() -> tmc::task<void> {
    using qerr = tmc::qu_mpsc_unbounded_err;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    {
      auto v = q.try_pull();
      EXPECT_EQ(qerr::EMPTY, v.status());
      EXPECT_FALSE(static_cast<bool>(v));
    }

    q.post(static_cast<size_t>(7));
    {
      auto v2 = q.try_pull();
      EXPECT_EQ(qerr::OK, v2.status());
      EXPECT_TRUE(static_cast<bool>(v2));
    }

    {
      auto v3 = q.try_pull();
      EXPECT_EQ(qerr::EMPTY, v3.status());
    }
    co_return;
  }());
}

// Concurrent post_bulk + close. Verifies the bulk-reservation close protocol:
// each post_bulk that returns true must be fully drained; post_bulks issued
// after close return false.
TEST_F(CATEGORY, close_concurrent_post_bulk) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t NBULKS = 200;
    static constexpr size_t BULK_SIZE = 7;
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};
    std::atomic<size_t> posted_ok{0};

    auto results = co_await tmc::spawn_tuple(
      [&](auto& Q) -> tmc::task<void> {
        size_t vals[BULK_SIZE];
        for (size_t i = 0; i < BULK_SIZE; ++i) {
          vals[i] = 1;
        }
        for (size_t i = 0; i < NBULKS; ++i) {
          if (Q.post_bulk(&vals[0], BULK_SIZE)) {
            posted_ok.fetch_add(BULK_SIZE, std::memory_order_relaxed);
          }
          if ((i & 0xF) == 0) {
            co_await tmc::reschedule();
          }
        }
        Q.close();
        EXPECT_FALSE(Q.post_bulk(&vals[0], BULK_SIZE));
        co_return;
      }(q),
      [](auto& Q) -> tmc::task<size_t> {
        size_t pulled = 0;
        while (true) {
          auto v = co_await Q.pull();
          if (!v) {
            co_return pulled;
          }
          pulled += *v;
        }
      }(q)
    );

    size_t pulled = std::get<1>(results);
    EXPECT_EQ(posted_ok.load(), pulled);
    EXPECT_EQ(NBULKS * BULK_SIZE, pulled);
    co_return;
  }());
}

// Type that requires multiple arguments to construct. Used to verify that
// post() forwards a variadic argument pack and constructs T in-place.
struct mpsc_multi_arg {
  size_t a;
  size_t b;
  size_t c;
  mpsc_multi_arg(size_t A, size_t B, size_t C) noexcept : a{A}, b{B}, c{C} {}
  mpsc_multi_arg(size_t A, size_t B) noexcept : a{A}, b{B}, c{0} {}
};

// post() forwards variadic arguments and constructs T in-place.
TEST_F(CATEGORY, post_variadic_args) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<mpsc_multi_arg, q_config<0>>{};

    // Single-argument form: construct from an existing T.
    EXPECT_TRUE(q.post(mpsc_multi_arg{1, 2, 3}));
    // Two-argument form.
    EXPECT_TRUE(q.post(static_cast<size_t>(4), static_cast<size_t>(5)));
    // Three-argument form: construct in-place from (size_t, size_t, size_t).
    EXPECT_TRUE(q.post(
      static_cast<size_t>(6), static_cast<size_t>(7), static_cast<size_t>(8)
    ));

    {
      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(1u, v->a);
      EXPECT_EQ(2u, v->b);
      EXPECT_EQ(3u, v->c);
    }
    {
      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(4u, v->a);
      EXPECT_EQ(5u, v->b);
      EXPECT_EQ(0u, v->c);
    }
    {
      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      EXPECT_EQ(6u, v->a);
      EXPECT_EQ(7u, v->b);
      EXPECT_EQ(8u, v->c);
    }
    co_return;
  }());
}

// post_bulk(Begin, End) iterator-pair overload.
TEST_F(CATEGORY, post_bulk_iter_pair) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    std::vector<size_t> vals;
    for (size_t i = 0; i < 10; ++i) {
      vals.push_back(i);
    }
    EXPECT_TRUE(q.post_bulk(vals.begin(), vals.end()));

    size_t count = 0;
    size_t sum = 0;
    for (size_t i = 0; i < 10; ++i) {
      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(10u, count);
    EXPECT_EQ(0u + 1u + 2u + 3u + 4u + 5u + 6u + 7u + 8u + 9u, sum);

    auto empty = q.try_pull();
    EXPECT_FALSE(static_cast<bool>(empty));
    co_return;
  }());
}

// post_bulk(Range) range overload.
TEST_F(CATEGORY, post_bulk_range) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    std::vector<size_t> vals;
    for (size_t i = 0; i < 10; ++i) {
      vals.push_back(i);
    }
    EXPECT_TRUE(q.post_bulk(vals));

    size_t count = 0;
    size_t sum = 0;
    for (size_t i = 0; i < 10; ++i) {
      auto v = q.try_pull();
      EXPECT_TRUE(static_cast<bool>(v));
      sum += *v;
      ++count;
    }
    EXPECT_EQ(10u, count);
    EXPECT_EQ(0u + 1u + 2u + 3u + 4u + 5u + 6u + 7u + 8u + 9u, sum);

    auto empty = q.try_pull();
    EXPECT_FALSE(static_cast<bool>(empty));
    co_return;
  }());
}

// All 3 post_bulk forms accept zero-element inputs and become no-ops.
TEST_F(CATEGORY, post_bulk_empty_all_forms) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto q = tmc::qu_mpsc_unbounded<size_t, q_config<0>>{};

    // (iter, count) form with count == 0.
    size_t dummy = 0;
    EXPECT_TRUE(q.post_bulk(&dummy, 0));

    // (begin, end) form with begin == end.
    std::vector<size_t> empty_vec;
    EXPECT_TRUE(q.post_bulk(empty_vec.begin(), empty_vec.end()));

    // (range) form with an empty range.
    EXPECT_TRUE(q.post_bulk(empty_vec));

    // None of the calls should have produced any elements.
    auto v = q.try_pull();
    EXPECT_FALSE(static_cast<bool>(v));

    // The queue must still be usable after the no-op bulk posts.
    EXPECT_TRUE(q.post(static_cast<size_t>(42)));
    auto v2 = q.try_pull();
    EXPECT_TRUE(static_cast<bool>(v2));
    EXPECT_EQ(42u, *v2);
    co_return;
  }());
}

#undef CATEGORY

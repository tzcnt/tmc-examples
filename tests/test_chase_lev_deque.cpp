// Tests for tmc::detail::chase_lev_deque.
//
// Covers single-threaded correctness (push/pop LIFO at the tail,
// steal FIFO at the head), buffer growth, post_bulk, the CAS race on
// the last element, index wrap-around, and multi-threaded
// owner-vs-stealers scenarios.

#include "tmc/all_headers.hpp" // IWYU pragma: keep

#include <gtest/gtest.h>

#include <atomic>
#include <numeric>
#include <set>
#include <thread>
#include <vector>

#define CATEGORY test_chase_lev_deque_32bit

using tmc::detail::chase_lev_deque;

namespace {

// Helper: drain everything the owner can pop and return it in pop order
// (which is LIFO, i.e. reverse of the push order).
template <typename T> std::vector<T> drain_pop(chase_lev_deque<T>& Q) {
  std::vector<T> out;
  T v{};
  while (Q.try_pop(v)) {
    out.push_back(v);
  }
  return out;
}

} // namespace

class CATEGORY : public testing::Test {};

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_F(CATEGORY, default_construct_is_empty) {
  chase_lev_deque<size_t> q;
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(0u, q.size_approx());

  size_t v = 42;
  EXPECT_FALSE(q.try_pop(v));
  EXPECT_FALSE(q.steal(v));
}

TEST_F(CATEGORY, custom_initial_capacity_rounds_up_to_power_of_two) {
  // 3 -> rounds up to 4; we should be able to push 4 items without
  // triggering a grow (we cannot directly observe capacity from outside,
  // but we can at least confirm operations succeed).
  chase_lev_deque<size_t> q(3);
  for (size_t i = 0; i < 4; ++i) {
    q.push(i);
  }
  EXPECT_EQ(4u, q.size_approx());
}

TEST_F(CATEGORY, capacity_exactly_one) {
  chase_lev_deque<size_t> q(1);
  q.push(7u);
  // Force a grow on the next push (capacity == 1).
  q.push(8u);
  EXPECT_EQ(2u, q.size_approx());

  size_t v = 0;
  EXPECT_TRUE(q.try_pop(v));
  EXPECT_EQ(8, v);
  EXPECT_TRUE(q.try_pop(v));
  EXPECT_EQ(7, v);
  EXPECT_FALSE(q.try_pop(v));
}

// ---------------------------------------------------------------------------
// Single-threaded push / pop / steal
// ---------------------------------------------------------------------------

TEST_F(CATEGORY, push_then_pop_is_lifo) {
  chase_lev_deque<size_t> q;
  for (size_t i = 0; i < 10; ++i) {
    q.push(i);
  }
  EXPECT_EQ(10u, q.size_approx());

  auto popped = drain_pop(q);
  ASSERT_EQ(10u, popped.size());
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(9 - i, popped[static_cast<size_t>(i)]);
  }
  EXPECT_TRUE(q.empty());
}

TEST_F(CATEGORY, push_then_steal_is_fifo) {
  chase_lev_deque<size_t> q;
  for (size_t i = 0; i < 10; ++i) {
    q.push(i);
  }

  std::vector<size_t> stolen;
  size_t v = 0;
  while (q.steal(v)) {
    stolen.push_back(v);
  }
  ASSERT_EQ(10u, stolen.size());
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_EQ(i, stolen[static_cast<size_t>(i)]);
  }
  EXPECT_TRUE(q.empty());
}

TEST_F(CATEGORY, alternating_push_pop) {
  chase_lev_deque<size_t> q;
  size_t v = 0;
  for (size_t i = 0; i < 100; ++i) {
    q.push(i);
    EXPECT_TRUE(q.try_pop(v));
    EXPECT_EQ(i, v);
    EXPECT_TRUE(q.empty());
  }
}

TEST_F(CATEGORY, steal_then_pop_until_empty) {
  chase_lev_deque<size_t> q;
  for (size_t i = 0; i < 8; ++i) {
    q.push(i);
  }
  size_t v = 0;
  // Steal from the head.
  EXPECT_TRUE(q.steal(v));
  EXPECT_EQ(0, v);
  EXPECT_TRUE(q.steal(v));
  EXPECT_EQ(1, v);

  // Pop the rest from the tail in LIFO order.
  auto popped = drain_pop(q);
  std::vector<size_t> expected{7, 6, 5, 4, 3, 2};
  EXPECT_EQ(expected, popped);
}

// ---------------------------------------------------------------------------
// Buffer growth
// ---------------------------------------------------------------------------

TEST_F(CATEGORY, push_grows_buffer) {
  // Start small so growth triggers fast and many times.
  chase_lev_deque<size_t> q(2);
  constexpr size_t N = 10000;
  for (size_t i = 0; i < N; ++i) {
    q.push(i);
  }
  EXPECT_EQ(static_cast<size_t>(N), q.size_approx());

  // Stealing should still return items in FIFO order.
  std::vector<size_t> stolen;
  size_t v = 0;
  while (q.steal(v)) {
    stolen.push_back(v);
  }
  ASSERT_EQ(static_cast<size_t>(N), stolen.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_EQ(i, stolen[static_cast<size_t>(i)]);
  }
}

TEST_F(CATEGORY, grow_preserves_partial_window) {
  // Push, pop some, then push enough to force a grow. The remaining items
  // (which live at non-zero indices in the old buffer) must be copied to
  // the correct positions in the new buffer.
  chase_lev_deque<size_t> q(4); // capacity 4
  for (size_t i = 0; i < 4; ++i) {
    q.push(i); // contents: [0,1,2,3]
  }
  size_t v = 0;
  EXPECT_TRUE(q.steal(v)); // remove 0 from head -> top=1
  EXPECT_EQ(0, v);
  EXPECT_TRUE(q.steal(v)); // remove 1 from head -> top=2
  EXPECT_EQ(1, v);
  // Now logical contents are [2,3] but at indices 2,3 in the old buffer.
  // Push 3 more -> would exceed capacity 4 and trigger grow.
  q.push(4u);
  q.push(5u);
  q.push(6u); // <-- forces grow
  EXPECT_EQ(5u, q.size_approx());

  // Stealing from head must produce 2,3,4,5,6.
  std::vector<size_t> stolen;
  while (q.steal(v)) {
    stolen.push_back(v);
  }
  std::vector<size_t> expected{2, 3, 4, 5, 6};
  EXPECT_EQ(expected, stolen);
}

// ---------------------------------------------------------------------------
// post_bulk
// ---------------------------------------------------------------------------

TEST_F(CATEGORY, post_bulk_zero_is_noop) {
  chase_lev_deque<size_t> q;
  std::vector<size_t> src{1, 2, 3};
  q.post_bulk(src.begin(), 0);
  EXPECT_TRUE(q.empty());
  size_t v = 0;
  EXPECT_FALSE(q.try_pop(v));
  EXPECT_FALSE(q.steal(v));
}

TEST_F(CATEGORY, post_bulk_basic) {
  chase_lev_deque<size_t> q;
  std::vector<size_t> src(20);
  std::iota(src.begin(), src.end(), 100); // 100..119
  q.post_bulk(src.begin(), src.size());
  EXPECT_EQ(20u, q.size_approx());

  // Steal in order.
  size_t v = 0;
  for (size_t i = 0; i < 20; ++i) {
    ASSERT_TRUE(q.steal(v));
    EXPECT_EQ(100 + i, v);
  }
  EXPECT_TRUE(q.empty());
}

TEST_F(CATEGORY, post_bulk_grows_to_exact_fit) {
  // Initial capacity 2, post 1000 items in one bulk -> grow must
  // double until newCap >= needed. Verify all items are recoverable.
  chase_lev_deque<size_t> q(2);
  std::vector<size_t> src(1000);
  std::iota(src.begin(), src.end(), 0);
  q.post_bulk(src.begin(), src.size());
  EXPECT_EQ(1000u, q.size_approx());

  size_t v = 0;
  for (size_t i = 0; i < 1000; ++i) {
    ASSERT_TRUE(q.steal(v));
    EXPECT_EQ(i, v);
  }
}

TEST_F(CATEGORY, mixed_push_and_post_bulk) {
  chase_lev_deque<size_t> q(4);
  q.push(1u);
  q.push(2u);
  std::vector<size_t> src{3, 4, 5, 6, 7};
  q.post_bulk(src.begin(), src.size());
  q.push(8u);

  std::vector<size_t> stolen;
  size_t v = 0;
  while (q.steal(v)) {
    stolen.push_back(v);
  }
  std::vector<size_t> expected{1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_EQ(expected, stolen);
}

TEST_F(CATEGORY, post_bulk_when_partially_drained_grows_correctly) {
  // Drain part of the queue then bulk-push enough to grow.
  chase_lev_deque<size_t> q(4);
  for (size_t i = 0; i < 4; ++i) {
    q.push(i);
  }
  size_t v = 0;
  EXPECT_TRUE(q.steal(v));
  EXPECT_EQ(0, v);

  // Logical contents [1,2,3] at indices 1,2,3. Bulk-push 20 -> must grow.
  std::vector<size_t> src(20);
  std::iota(src.begin(), src.end(), 10);
  q.post_bulk(src.begin(), src.size());

  std::vector<size_t> stolen;
  while (q.steal(v)) {
    stolen.push_back(v);
  }
  std::vector<size_t> expected{1, 2, 3};
  for (size_t i = 0; i < 20; ++i) {
    expected.push_back(10 + i);
  }
  EXPECT_EQ(expected, stolen);
}

TEST_F(CATEGORY, push_pop_wraparound) {
  chase_lev_deque<size_t> q(2);
  constexpr size_t N = 1 << 10; // 1k push/pop pairs
  size_t v = 0;
  for (size_t i = 0; i < N; ++i) {
    q.push(i);
    ASSERT_TRUE(q.try_pop(v));
    ASSERT_EQ(i, v);
  }
  EXPECT_TRUE(q.empty());
  EXPECT_FALSE(q.try_pop(v));
  EXPECT_FALSE(q.steal(v));
}

TEST_F(CATEGORY, push_steal_wraparound) {
  chase_lev_deque<size_t> q(2);
  constexpr size_t N = 1 << 10;
  size_t v = 0;
  for (size_t i = 0; i < N; ++i) {
    q.push(i);
    ASSERT_TRUE(q.steal(v));
    ASSERT_EQ(i, v);
  }
  EXPECT_TRUE(q.empty());
  EXPECT_FALSE(q.try_pop(v));
  EXPECT_FALSE(q.steal(v));
}

// ---------------------------------------------------------------------------
// size_approx / empty
// ---------------------------------------------------------------------------

TEST_F(CATEGORY, size_approx_and_empty_track_pushes_and_pops) {
  chase_lev_deque<size_t> q;
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(0u, q.size_approx());

  for (size_t i = 1; i <= 5; ++i) {
    q.push(i);
    EXPECT_FALSE(q.empty());
    EXPECT_EQ(static_cast<size_t>(i), q.size_approx());
  }

  size_t v = 0;
  for (size_t i = 5; i >= 1; --i) {
    EXPECT_EQ(static_cast<size_t>(i), q.size_approx());
    ASSERT_TRUE(q.try_pop(v));
  }
  EXPECT_TRUE(q.empty());
  EXPECT_EQ(0u, q.size_approx());
}

// ---------------------------------------------------------------------------
// Multi-threaded: owner pushes/pops, many stealers steal.
// Verify that every produced item is observed exactly once across the
// owner's pops and the stealers' steals.
// ---------------------------------------------------------------------------

TEST_F(CATEGORY, concurrent_owner_and_stealers) {
  constexpr size_t N = 50000;
  constexpr size_t NUM_STEALERS = 4;

  chase_lev_deque<size_t> q(8);

  std::atomic<bool> producer_done{false};
  std::vector<std::vector<size_t>> stolen(NUM_STEALERS);
  std::vector<size_t> popped;
  popped.reserve(N);

  std::vector<std::thread> stealers;
  stealers.reserve(NUM_STEALERS);
  for (size_t s = 0; s < NUM_STEALERS; ++s) {
    stealers.emplace_back([&, s]() {
      auto& out = stolen[static_cast<size_t>(s)];
      out.reserve(N / NUM_STEALERS);
      size_t v = 0;
      while (true) {
        if (q.steal(v)) {
          out.push_back(v);
        } else {
          if (producer_done.load(std::memory_order_acquire) && q.empty()) {
            // Drain any remainder racing with the producer's completion.
            while (q.steal(v)) {
              out.push_back(v);
            }
            return;
          }
          std::this_thread::yield();
        }
      }
    });
  }

  // Owner thread: push N items, occasionally popping a few back.
  std::thread owner([&]() {
    size_t v = 0;
    for (size_t i = 0; i < N; ++i) {
      q.push(i);
      // Every 17 pushes, try to pop one back so we exercise pop too.
      if ((i % 17) == 0) {
        if (q.try_pop(v)) {
          popped.push_back(v);
        }
      }
    }
    // Drain owner's tail.
    while (q.try_pop(v)) {
      popped.push_back(v);
    }
    producer_done.store(true, std::memory_order_release);
  });

  owner.join();
  for (auto& t : stealers) {
    t.join();
  }

  // Now every value in [0, N) must appear exactly once across popped
  // and all stolen vectors.
  size_t total = popped.size();
  for (auto& s : stolen) {
    total += s.size();
  }
  EXPECT_EQ(static_cast<size_t>(N), total);

  std::vector<bool> seen(N, false);
  auto mark = [&](size_t v) {
    ASSERT_GE(v, 0);
    ASSERT_LT(v, N);
    ASSERT_FALSE(seen[static_cast<size_t>(v)])
      << "value " << v << " duplicated";
    seen[static_cast<size_t>(v)] = true;
  };
  for (size_t v : popped) {
    mark(v);
  }
  for (auto& s : stolen) {
    for (size_t v : s) {
      mark(v);
    }
  }
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(seen[static_cast<size_t>(i)]) << "value " << i << " missing";
  }
}

// Bulk-push concurrent with multiple stealers.
TEST_F(CATEGORY, concurrent_post_bulk_with_stealers) {
  constexpr size_t N = 50000;
  constexpr size_t CHUNK = 64;
  constexpr size_t NUM_STEALERS = 4;

  chase_lev_deque<size_t> q(8);
  std::atomic<bool> producer_done{false};
  std::vector<std::vector<size_t>> stolen(NUM_STEALERS);
  std::vector<size_t> owner_popped;

  std::vector<std::thread> stealers;
  stealers.reserve(NUM_STEALERS);
  for (size_t s = 0; s < NUM_STEALERS; ++s) {
    stealers.emplace_back([&, s]() {
      auto& out = stolen[static_cast<size_t>(s)];
      size_t v = 0;
      while (true) {
        if (q.steal(v)) {
          out.push_back(v);
        } else {
          if (producer_done.load(std::memory_order_acquire) && q.empty()) {
            while (q.steal(v)) {
              out.push_back(v);
            }
            return;
          }
          std::this_thread::yield();
        }
      }
    });
  }

  // Owner thread: bulk-post in chunks until N items have been posted.
  std::thread owner([&]() {
    std::vector<size_t> buf(CHUNK);
    size_t next = 0;
    while (next < N) {
      size_t sz = std::min(CHUNK, N - next);
      for (size_t i = 0; i < sz; ++i) {
        buf[static_cast<size_t>(i)] = next + i;
      }
      q.post_bulk(buf.begin(), static_cast<size_t>(sz));
      next += sz;
    }
    // Owner drains anything still in the tail.
    size_t v = 0;
    while (q.try_pop(v)) {
      owner_popped.push_back(v);
    }
    producer_done.store(true, std::memory_order_release);
  });

  owner.join();
  for (auto& t : stealers) {
    t.join();
  }

  std::set<size_t> seen;
  for (size_t v : owner_popped) {
    ASSERT_TRUE(seen.insert(v).second) << "duplicate value " << v;
  }
  for (auto& s : stolen) {
    for (size_t v : s) {
      ASSERT_TRUE(seen.insert(v).second) << "duplicate value " << v;
    }
  }
  EXPECT_EQ(static_cast<size_t>(N), seen.size());
  for (size_t i = 0; i < N; ++i) {
    EXPECT_TRUE(seen.count(i) == 1) << "missing value " << i;
  }
}

#undef CATEGORY

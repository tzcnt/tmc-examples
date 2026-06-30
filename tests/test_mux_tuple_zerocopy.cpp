// Tests for the interaction between tmc::mux_tuple and the zero-copy scopes
// returned by tmc::qu_mpsc_unbounded::pull().
//
// The hazard being guarded against:
//
//   pull() returns an `aw_pull`, which mux_tuple wraps in a task that runs
//   `co_return co_await aw_pull`. The wrapper acquires the queue read ticket
//   (get_read_ticket -> reads read_offset) in aw_pull's await_ready(), at the
//   START of the wrapper, but the resulting pull_zc_scope is only stored into
//   the mux slot at co_return, via the slot's move-assignment - and that
//   move-assignment is what releases (finish_read) the PREVIOUS scope.
//
//   So in the natural "consume then re-fork" loop:
//
//     tmc::mux_tuple mux(q.pull());
//     while (true) {
//       co_await mux;
//       auto& r = mux.get<0>();
//       if (!r) break;
//       process(*r);
//       mux.fork<0>(q.pull());   // <-- starts the next pull while the slot
//     }                          //     still holds the previous scope
//
//   the next pull's read ticket is acquired while the previous scope is still
//   live in the slot. That violates the queue's documented contract ("This
//   scope must be released before the next call to try_pull() or pull()") and
//   causes the next pull to re-read the same not-yet-released element.
//
// These tests verify the corrected behavior without relying on any debug
// instrumentation added to the queue. With the bug present they fail on their
// own terms: in a debug build the double-finish_read trips qu_storage's
// existing `exists` assert; in a release build the value/destruction-count
// EXPECTs catch the duplication directly.

#include "test_common.hpp"
#include "tmc/mux_many.hpp"
#include "tmc/mux_tuple.hpp"
#include "tmc/qu_mpsc_unbounded.hpp"

#include <atomic>
#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

#define CATEGORY test_mux_tuple_zerocopy

namespace {

class CATEGORY : public testing::Test {
protected:
  // A single-threaded executor makes the consume/re-fork interleaving fully
  // deterministic: each forked pull wrapper runs to completion (and resumes the
  // consumer) before the consumer can fork the next one.
  static void SetUpTestSuite() { tmc::cpu_executor().set_thread_count(1).init(); }
  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }
  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

// Small block size so the test also exercises block transitions / reclaim
// while the bug is in play.
struct zc_config : tmc::qu_mpsc_unbounded_default_config {
  static inline constexpr size_t BlockSize = 2;
  static inline constexpr bool ConsumerCanSuspend = true;
};

// Move-only, default-constructible element type that counts destructions.
struct counted {
  std::atomic<size_t>* count;
  size_t value;
  counted(std::atomic<size_t>* C, size_t V) noexcept : count{C}, value{V} {}
  counted(counted const&) = delete;
  counted& operator=(counted const&) = delete;
  ~counted() {
    if (count != nullptr) {
      count->fetch_add(1, std::memory_order_relaxed);
    }
  }
};

// Non-default-constructible (so its mux slot storage is std::optional<...>),
// move-only, destruction-counting result type. Used to prove that fork()'s
// "reset the slot" step handles the optional-wrapped storage: assigning an
// empty optional must destroy the contained value exactly once.
struct no_default_counted {
  std::atomic<size_t>* count;
  size_t value;
  no_default_counted() = delete;
  no_default_counted(std::atomic<size_t>* C, size_t V) noexcept : count{C}, value{V} {}
  no_default_counted(no_default_counted const&) = delete;
  no_default_counted& operator=(no_default_counted const&) = delete;
  no_default_counted(no_default_counted&& Other) noexcept
      : count{Other.count}, value{Other.value} {
    Other.count = nullptr;
  }
  no_default_counted& operator=(no_default_counted&& Other) noexcept {
    count = Other.count;
    value = Other.value;
    Other.count = nullptr;
    return *this;
  }
  ~no_default_counted() {
    if (count != nullptr) {
      count->fetch_add(1, std::memory_order_relaxed);
    }
  }
};

// fork() with an optional-wrapped result slot: a non-default-constructible
// result makes ResultStorage == std::optional<no_default_counted>. fork()'s
// reset (slot = SlotResult{}) assigns an empty optional, which must clear the
// slot - destroying the prior value exactly once - with no separate if constexpr
// branch for the optional case.
TEST_F(CATEGORY, fork_optional_slot_reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t N = 5;
    std::atomic<size_t> destroyed{0};
    {
      auto make = [](std::atomic<size_t>* D, size_t V) -> tmc::task<no_default_counted> {
        co_return no_default_counted{D, V};
      };

      tmc::mux_tuple<tmc::task<no_default_counted>> mux;
      mux.fork<0>(make(&destroyed, 0));
      for (size_t k = 0; k < N; ++k) {
        size_t idx = co_await mux;
        EXPECT_EQ(0u, idx);
        auto& slot = mux.get<0>(); // std::optional<no_default_counted>&
        EXPECT_TRUE(slot.has_value());
        EXPECT_EQ(k, slot->value);
        if (k + 1 < N) {
          // Resets slot (destroys item k's optional value), then re-forks.
          mux.fork<0>(make(&destroyed, k + 1));
        }
      }
      // The last value stays in the slot until the mux is destroyed here.
    }
    EXPECT_EQ(N, destroyed.load());
    co_return;
  }());
}

// Drains a pre-filled, closed queue through a single-slot mux_tuple using the
// consume-then-fork idiom and verifies every value is seen exactly once, in
// order. With the bug present, the debug guard fires (debug builds) or values
// are duplicated / dropped (NDEBUG builds).
TEST_F(CATEGORY, fork_loop_no_duplication) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t N = 7;
    auto q = tmc::qu_mpsc_unbounded<size_t, zc_config>{};
    for (size_t i = 0; i < N; ++i) {
      EXPECT_TRUE(q.post(i));
    }
    q.close();

    std::vector<size_t> got;
    tmc::mux_tuple mux(q.pull());
    while (true) {
      size_t idx = co_await mux;
      EXPECT_EQ(0u, idx); // only one slot
      auto& r = mux.get<0>();
      if (!r) {
        break; // closed and drained
      }
      got.push_back(*r);
      // Safety net so a buggy build (where values repeat forever) terminates.
      if (got.size() > 4 * N) {
        ADD_FAILURE() << "drain did not terminate; values are being duplicated";
        break;
      }
      mux.fork<0>(q.pull());
    }

    EXPECT_EQ(N, got.size());
    for (size_t i = 0; i < N && i < got.size(); ++i) {
      EXPECT_EQ(i, got[i]) << "value at position " << i;
    }
    co_return;
  }());
}

// Same idiom, but with a destruction-counting element type: each queued value
// must be destroyed exactly once. With the bug present, the slot is
// double-finish_read'd, so values are destroyed more than once.
TEST_F(CATEGORY, fork_loop_destruct_once) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t N = 7;
    std::atomic<size_t> destroyed{0};

    {
      auto q = tmc::qu_mpsc_unbounded<counted, zc_config>{};
      for (size_t i = 0; i < N; ++i) {
        EXPECT_TRUE(q.post(&destroyed, i));
      }
      q.close();

      size_t seen = 0;
      tmc::mux_tuple mux(q.pull());
      while (true) {
        co_await mux;
        auto& r = mux.get<0>();
        if (!r) {
          break;
        }
        EXPECT_EQ(seen, r->value);
        ++seen;
        if (seen > 4 * N) {
          ADD_FAILURE() << "drain did not terminate";
          break;
        }
        mux.fork<0>(q.pull());
      }
      EXPECT_EQ(N, seen);
    }

    // Every value destroyed exactly once - no double frees, no leaks.
    EXPECT_EQ(N, destroyed.load());
    co_return;
  }());
}

// mux_many has the same consume-then-fork design as mux_tuple and the same
// hazard with zero-copy scopes; verify the homogeneous variant too.
TEST_F(CATEGORY, mux_many_fork_loop_no_duplication) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr size_t N = 7;
    using Q = tmc::qu_mpsc_unbounded<size_t, zc_config>;
    Q q;
    for (size_t i = 0; i < N; ++i) {
      EXPECT_TRUE(q.post(i));
    }
    q.close();

    std::vector<size_t> got;
    auto mux = tmc::mux_many<typename Q::pull_zc_scope, 1>();
    mux.fork(0, q.pull());
    while (true) {
      size_t idx = co_await mux;
      EXPECT_EQ(0u, idx);
      auto& r = mux[0];
      if (!r) {
        break;
      }
      got.push_back(*r);
      if (got.size() > 4 * N) {
        ADD_FAILURE() << "drain did not terminate; values are being duplicated";
        break;
      }
      mux.fork(0, q.pull());
    }

    EXPECT_EQ(N, got.size());
    for (size_t i = 0; i < N && i < got.size(); ++i) {
      EXPECT_EQ(i, got[i]) << "value at position " << i;
    }
    co_return;
  }());
}

} // namespace

#undef CATEGORY

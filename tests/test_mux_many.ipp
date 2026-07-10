#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "test_mux_common.hpp"
#include "test_spawn_many_common.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <numeric>
#include <ranges>
#include <vector>

// Tests for tmc::mux_many - a standalone result-multiplexer that eagerly
// initiates a homogeneous set of awaitables like tmc::spawn_many() but yields
// each result as it becomes ready, and also supports empty constructors (storage
// only) plus fork(idx, ...) to (re)launch work into individual slots.
//
// The eager-construction tests below are ported from the former
// tmc::spawn_many().result_each() tests. The fork()/empty-constructor tests
// mirror the tmc::mux_tuple tests; they exercise every value category (lvalue,
// movable rvalue, non-movable rvalue) and every runtime mode of
// tmc::detail::awaitable_traits<T>::mode that fork() can be handed (TMC_TASK,
// COROUTINE, ASYNC_INITIATE - both lvalue- and rvalue-qualified - and WRAPPER).
// The shared helper awaitables live in test_mux_common.hpp.

/*** Eager construction (initiates all awaitables like spawn_many()) ***/

template <int N> tmc::task<void> mux_many_static_sized_iterator() {
  auto iter = iter_of_static_size<N>();
  // We know that the iterator produces exactly N tasks.
  // Provide the Result type and Count (N) to mux_many, so that tasks and
  // results can be statically allocated in std::array.
  std::array<int, N> results;
  auto mux = tmc::mux_many<int, N>(iter.begin());
  for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
    results[idx] = mux[idx];
  }

  [[maybe_unused]] auto sum = std::accumulate(results.begin(), results.end(), 0);

  EXPECT_EQ(sum, (1 << N) - 1);

  co_return;
}

template <int N> tmc::task<void> mux_many_static_bounded_iterator() {
  // In this example, we do not know the exact number of tasks that iter could
  // produce. The template parameter N serves as an upper bound on the number
  // of tasks that will be spawned. We also need to manually count the number
  // of tasks spawned. There are 2 sub-examples.
  {
    // Sub-Example 1: Iterator produces less than N tasks.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });
    std::array<int, N> results;
    auto mux = tmc::mux_many<int, N>(iter.begin(), iter.end());
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results[idx] = mux[idx];
    }

    // At this point, taskCount == 4 and N == 5.
    // The last element of results will be left default-initialized.
    EXPECT_EQ(taskCount, N - 1);

    // This extra work yields a performance benefit, because we can still use
    // std::array with an unknown-sized iterator that spawns "up to N" tasks.
    [[maybe_unused]] auto sum = std::accumulate(
      results.begin(), results.begin() + static_cast<ptrdiff_t>(taskCount), 0
    );
    EXPECT_EQ(sum, (1 << N) - 1 - 8);
  }
  {
    // Sub-Example 2: Iterator could produce more than N tasks.
    // Only the first N will be taken.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N + 20>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });

    std::array<int, N> results;
    auto mux = tmc::mux_many<int, N>(iter.begin(), iter.end());
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results[idx] = mux[idx];
    }

    // At this point, taskCount == 5 and N == 5.
    // We stopped consuming elements from the iterator after N tasks.
    EXPECT_EQ(taskCount, N);
    [[maybe_unused]] auto sum = std::accumulate(
      results.begin(), results.begin() + static_cast<ptrdiff_t>(taskCount), 0
    );
    EXPECT_EQ(sum, (1 << N) - 1 - 8 + (1 << N));
  }
  co_return;
}

template <int N> tmc::task<void> mux_many_dynamic_known_sized_iterator() {
  auto iter = iter_of_dynamic_known_size<N>();

  // The template parameter N to mux_many is not provided.
  // This overload will produce a right-sized output vector
  // (internally calculated from tasks.end() - tasks.begin())
  std::vector<int> results;
  auto mux = tmc::mux_many(iter.begin(), iter.end());
  for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
    results.push_back(mux[idx]);
  }

  [[maybe_unused]] auto taskCount = static_cast<size_t>(iter.end() - iter.begin());

  [[maybe_unused]] auto sum = std::accumulate(results.begin(), results.end(), 0);
  EXPECT_EQ(sum, (1 << N) - 1 - 8);
  EXPECT_EQ(results.size(), taskCount);

  co_return;
}

template <int N> tmc::task<void> mux_many_dynamic_unknown_sized_iterator() {
  auto iter = iter_of_dynamic_unknown_size<N>();

  // Due to unpredictable_filter(), we cannot know the exact number of tasks.
  // We do not provide the N template parameter, and the size is unknown.
  std::vector<int> results;
  auto mux = tmc::mux_many(iter.begin(), iter.end());
  for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
    results.push_back(mux[idx]);
  }

  [[maybe_unused]] auto sum = std::accumulate(results.begin(), results.end(), 0);

  EXPECT_EQ(sum, (1 << N) - 1 - 8);

  co_return;
}

template <int N> tmc::task<void> mux_many_dynamic_bounded_iterator() {
  // In this example, we do not know the exact number of tasks that iter could
  // produce. The 3rd parameter MaxTasks serves as an upper bound on the number
  // of tasks that will be spawned. We also need to manually count the number of
  // tasks spawned. There are 2 sub-examples.
  size_t MaxTasks = N;
  {
    // Sub-Example 1: Iterator produces less than MaxTasks tasks.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });
    std::vector<int> results;
    auto mux = tmc::mux_many(iter.begin(), iter.end(), MaxTasks);
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results.push_back(mux[idx]);
    }

    // At this point, taskCount == 4 and N == 5.
    EXPECT_EQ(taskCount, MaxTasks - 1);

    [[maybe_unused]] auto sum = std::accumulate(
      results.begin(), results.begin() + static_cast<ptrdiff_t>(taskCount), 0
    );
    EXPECT_EQ(sum, (1 << N) - 1 - 8);
    EXPECT_EQ(results.size(), taskCount);
  }
  {
    // Sub-Example 2: Iterator could produce more than MaxTasks tasks.
    // Only the first MaxTasks will be taken.
    size_t taskCount = 0;
    auto iter = iter_of_dynamic_unknown_size<N + 20>() |
                std::ranges::views::transform([&taskCount](auto t) {
                  ++taskCount;
                  return t;
                });
    std::vector<int> results;
    auto mux = tmc::mux_many(iter.begin(), iter.end(), MaxTasks);
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results.push_back(mux[idx]);
    }

    // At this point, taskCount == 5 and N == 5.
    // We stopped consuming elements from the iterator after N tasks.
    EXPECT_EQ(taskCount, N);
    [[maybe_unused]] auto sum = std::accumulate(
      results.begin(), results.begin() + static_cast<ptrdiff_t>(taskCount), 0
    );
    EXPECT_EQ(sum, (1 << N) - 1 - 8 + (1 << N));
    EXPECT_EQ(results.size(), taskCount);
  }
  co_return;
}

TEST_F(CATEGORY, mux_many_static_sized_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await mux_many_static_sized_iterator<5>();
  }());
}

TEST_F(CATEGORY, mux_many_static_bounded_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await mux_many_static_bounded_iterator<5>();
  }());
}

TEST_F(CATEGORY, mux_many_dynamic_known_sized_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await mux_many_dynamic_known_sized_iterator<5>();
  }());
}

TEST_F(CATEGORY, mux_many_dynamic_unknown_sized_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await mux_many_dynamic_unknown_sized_iterator<5>();
  }());
}

TEST_F(CATEGORY, mux_many_dynamic_bounded_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await mux_many_dynamic_bounded_iterator<5>();
  }());
}

// Every eagerly-initiated task completes (awaited via `aa`) before the group is
// drained, so the first co_await of the group finds all results already ready.
TEST_F(CATEGORY, mux_many_resume_after) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr int N = 5;
    for (int i = 0; i < N; ++i) {
      atomic_awaitable<int> aa(i);
      auto iter = std::ranges::views::iota(0, i) |
                  std::ranges::views::transform([&aa](int idx) -> tmc::task<int> {
                    return [](int I, atomic_awaitable<int>& AA) -> tmc::task<int> {
                      AA.inc();
                      co_return 1 << I;
                    }(idx, aa);
                  });
      auto mux = tmc::mux_many(iter);
      co_await aa;
      std::vector<int> results(static_cast<size_t>(i), 0);
      for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
        results[idx] = mux[idx];
      }

      [[maybe_unused]] auto sum = std::accumulate(results.begin(), results.end(), 0);

      EXPECT_EQ(sum, (1 << i) - 1);
    }

    co_return;
  }());
}

// Eager construction from an iterator of WRAPPER-mode awaitables. The WRAPPERs
// are routed through into_known()/safe_wrap() in the constructor and
// bulk-submitted as work items.
TEST_F(CATEGORY, mux_many_eager_wrapper_mode) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::array<mux_wrapper_int, 3> arr{
      mux_wrapper_int{1}, mux_wrapper_int{2}, mux_wrapper_int{4}
    };
    std::array<int, 3> results{};
    auto mux = tmc::mux_many<int, 3>(arr.begin());
    int count = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results[idx] = mux[idx];
      ++count;
    }
    EXPECT_EQ(count, 3);
    EXPECT_EQ(std::accumulate(results.begin(), results.end(), 0), 7);
  }());
}

// Eager construction from an unknown-sized iterator of COROUTINE-mode
// awaitables. The fixed-size std::array result storage keeps result pointers
// stable, so each awaitable is prepared and type-erased into a work_item in a
// single pass, then bulk-submitted.
TEST_F(CATEGORY, mux_many_eager_coroutine_unknown) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr int N = 5;
    auto iter =
      std::ranges::views::iota(0, N) | std::ranges::views::filter(unpredictable_filter) |
      std::ranges::views::transform([](int i) { return mux_coroutine_op<int>{work(i)}; });
    std::vector<int> results;
    auto mux = tmc::mux_many(iter.begin(), iter.end());
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results.push_back(mux[idx]);
    }
    // unpredictable_filter() drops i == 3, so task 3's value (1 << 3 == 8) is
    // not produced.
    EXPECT_EQ(std::accumulate(results.begin(), results.end(), 0), (1 << N) - 1 - 8);
  }());
}

// Eager construction from a non-subtractable iterator of ASYNC_INITIATE
// awaitables. The count cannot be computed up front (no operator-), and the
// awaitable is not default-constructible so it cannot be buffered in an array.
// The constructor must therefore initiate each awaitable individually and set
// the tracking variables only after the actual count is known - each awaitable
// completes synchronously inside async_initiate(), before that happens.
TEST_F(CATEGORY, mux_many_eager_async_initiate_unknown_sized_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    static constexpr int N = 5;
    struct op_iter {
      int i;
      using value_type = mux_immediate_async_op;
      value_type operator*() const { return mux_immediate_async_op(1 << i); }
      op_iter& operator++() {
        ++i;
        return *this;
      }
      bool operator!=(const op_iter& Other) const { return i != Other.i; }
    };

    std::array<int, N> results{};
    auto mux = tmc::mux_many(op_iter{0}, op_iter{N});
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results[idx] = mux[idx];
    }
    EXPECT_EQ(std::accumulate(results.begin(), results.end(), 0), (1 << N) - 1);

    // The deferred bookkeeping must reflect the actual initiated count:
    // the drained slots remain valid to fork() into.
    mux.fork(N - 1, mux_immediate_async_op(100));
    size_t idx = co_await mux;
    EXPECT_EQ(idx, static_cast<size_t>(N - 1));
    EXPECT_EQ(mux[idx], 100);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Eager construction from an iterator + runtime count (the mux_many(begin,
// count) overload). The count exceeds the bitmask capacity (63 / 31), so the
// group is clamped to TMC_PLATFORM_BITS - 1 slots. Confirms the clamp and that
// exactly that many results are produced.
TEST_F(CATEGORY, mux_many_iterator_count_clamped) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto mux = tmc::mux_many(
      tmc::iter_adapter(0, [](int) -> tmc::task<int> { co_return 1; }),
      static_cast<size_t>(TMC_PLATFORM_BITS + 16)
    );
    int sum = 0;
    int count = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      sum += mux[idx];
      ++count;
    }
    EXPECT_EQ(count, TMC_PLATFORM_BITS - 1);
    EXPECT_EQ(sum, TMC_PLATFORM_BITS - 1);
  }());
}

// The mux captures its continuation executor at each co_await, not at
// construction. Construct (and eagerly initiate on) ex(), then move to a
// second executor before awaiting: every co_await must resume on the second
// executor, including results from slots forked while running there.
TEST_F(CATEGORY, mux_many_await_on_second_executor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu exec2;
    exec2.set_thread_count(1).init();

    std::array<tmc::task<int>, 2> tasks{work(0), work(1)};
    auto mux = tmc::mux_many<int, 2>(tasks.begin(), tasks.end());

    co_await tmc::resume_on(exec2);
    EXPECT_EQ(tmc::current_executor(), exec2.type_erased());

    int sum = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      EXPECT_EQ(tmc::current_executor(), exec2.type_erased());
      sum += mux[idx];
    }
    // The end()-returning co_await also resumes here.
    EXPECT_EQ(tmc::current_executor(), exec2.type_erased());
    EXPECT_EQ(sum, 3);

    // Fork a replacement that runs on ex(); its completion must still resume
    // the awaiter on exec2.
    mux.fork(0, work(2), ex());
    auto idx = co_await mux;
    EXPECT_EQ(tmc::current_executor(), exec2.type_erased());
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[idx], 4);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());

    co_await tmc::resume_on(ex());
  }());
}

// Count exceeds the initially-dispatched task count. The fork() index limit is
// Count, not the initiated count, so the higher-indexed slot that was never
// started is a valid fork() target before the mux has ever been awaited.
TEST_F(CATEGORY, mux_many_fork_unused_higher_slot) {
  test_async_main(ex(), []() -> tmc::task<void> {
    std::array<tmc::task<int>, 2> tasks{work(0), work(1)};
    auto mux = tmc::mux_many<int, 3>(tasks.begin(), tasks.end());
    EXPECT_FALSE(mux.is_active(2));
    mux.fork(2, work(2));

    std::array<int, 3> results{};
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      results[idx] = mux[idx];
    }
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 4);
  }());
}

// Eager construction from an empty, unknown-sized iterator of coroutine tasks.
// Exercises the taskCount == 0 early-out (no tasks are submitted; the first
// co_await immediately reports end()).
TEST_F(CATEGORY, mux_many_eager_empty_unknown_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto iter = iter_of_dynamic_unknown_size<0>();
    auto mux = tmc::mux_many(iter.begin(), iter.end());
    size_t idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Eager construction from an empty, unknown-sized iterator of COROUTINE-mode
// awaitables. Exercises the COROUTINE-mode taskCount == 0 early-out.
TEST_F(CATEGORY, mux_many_eager_empty_unknown_coroutine_iterator) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto iter =
      std::ranges::views::iota(0, 0) | std::ranges::views::filter(unpredictable_filter) |
      std::ranges::views::transform([](int i) { return mux_coroutine_op<int>{work(i)}; });
    auto mux = tmc::mux_many(iter.begin(), iter.end());
    size_t idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// capacity() reports the fixed number of slots (the `Count` template argument /
// std::array storage size). It is independent of how many awaitables were
// initiated or how many slots are currently active.
TEST_F(CATEGORY, mux_many_capacity) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Empty (storage-only) group with an explicit Count.
    auto mux4 = tmc::mux_many<int, 4>();
    EXPECT_EQ(mux4.capacity(), 4u);

    // Default Count is TMC_PLATFORM_BITS - 1.
    auto muxDefault = tmc::mux_many<int>();
    EXPECT_EQ(muxDefault.capacity(), static_cast<size_t>(TMC_PLATFORM_BITS - 1));

    // Zero-slot degenerate group.
    auto mux0 = tmc::mux_many<int, 0>();
    EXPECT_EQ(mux0.capacity(), 0u);

    // capacity() is the storage size, not the number of initiated awaitables:
    // this group has storage for 3 slots but only 2 are eagerly initiated. It
    // also does not change as the group is drained.
    std::array<tmc::task<int>, 2> tasks{work(0), work(1)};
    auto mux3 = tmc::mux_many<int, 3>(tasks.begin(), tasks.end());
    EXPECT_EQ(mux3.capacity(), 3u);
    int count = 0;
    for (auto idx = co_await mux3; idx != mux3.end(); idx = co_await mux3) {
      ++count;
      EXPECT_EQ(mux3.capacity(), 3u); // unchanged while draining
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(mux3.capacity(), 3u);
  }());
}

// poll() is a non-suspending check for a ready result with three outcomes: it
// consumes and returns the index of a ready slot (exactly as co_await would),
// returns pending() when results are pending but none is ready, or returns end()
// when no submitted results remain. A slot consumed by poll() is re-forkable.
//
// mux_immediate_async_op completes synchronously inside fork(), so its slot is
// ready the moment fork() returns - poll() observes it with no suspension or
// spinning, deterministically on every executor. The pending slot uses a blocker
// task that never completes until released, and is drained via co_await.
TEST_F(CATEGORY, mux_many_poll) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 99;
    };

    // An empty (never-forked) group has no pending results, so poll() reports
    // end() - not pending().
    auto mux = tmc::mux_many<int, 2>();
    EXPECT_EQ(mux.poll(), mux.end());

    atomic_awaitable<int> block(1);
    mux.fork(0, mux_immediate_async_op(1)); // ready synchronously
    mux.fork(1, blocker(block));            // stays pending until block.inc()

    // Slot 0 is ready; slot 1 is pending. poll() consumes slot 0.
    size_t idx = mux.poll();
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 1);
    EXPECT_FALSE(mux.is_active(0)); // consumed by poll(): now re-forkable

    // Slot 1 is still pending (block not yet incremented): pending(), not end().
    EXPECT_EQ(mux.poll(), mux.pending());

    // The slot poll() consumed can be re-armed, just like after co_await.
    mux.fork(0, mux_immediate_async_op(2));
    idx = mux.poll();
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 2);

    // Release slot 1 and drain the remainder with co_await.
    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    EXPECT_EQ(mux[1], 99);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());

    // Fully drained (remaining_count == 0): poll() reports end(), like the
    // terminal co_await.
    EXPECT_EQ(mux.poll(), mux.end());
  }());
}

/*** Empty constructors (storage only; fork(idx, ...) to launch work) ***/

// The empty constructor allocates result storage but initiates nothing. The
// Result type and Count must be provided explicitly. fork() then launches
// each slot, and slot 0 is forked again after its first result is consumed.
TEST_F(CATEGORY, mux_many_empty_constructor_fork_all) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 99;
    };

    auto mux = tmc::mux_many<int, 2>(); // storage only
    mux.fork(0, immediate(1));
    mux.fork(1, blocker(block));

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 1);

    mux.fork(0, immediate(2));
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 2);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    EXPECT_EQ(mux[1], 99);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// The empty constructor with a defaulted Count (TMC_PLATFORM_BITS - 1) allocates
// full-size std::array storage. Only a few of the slots are forked here.
TEST_F(CATEGORY, mux_many_empty_default_count_fork_all) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int V) -> tmc::task<int> { co_return V; };

    auto mux = tmc::mux_many<int>(); // Count defaults to TMC_PLATFORM_BITS - 1
    mux.fork(0, work(1));
    mux.fork(1, work(2));
    mux.fork(2, work(4));

    int sum = 0;
    int count = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      sum += mux[i];
      ++count;
    }
    EXPECT_EQ(count, 3);
    EXPECT_EQ(sum, 7);
  }());
}

// The defaulted Count is exactly TMC_PLATFORM_BITS - 1, so every index in
// [0, TMC_PLATFORM_BITS - 1) is a valid slot. This forks the lowest and the
// highest valid slot to confirm the full bitmask range is usable.
TEST_F(CATEGORY, mux_many_empty_default_count_highest_slot) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int V) -> tmc::task<int> { co_return V; };

    auto mux = tmc::mux_many<int>(); // Count defaults to TMC_PLATFORM_BITS - 1
    mux.fork(0, work(1));
    mux.fork(TMC_PLATFORM_BITS - 2, work(2)); // highest valid slot index

    int sum = 0;
    int count = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      sum += mux[i];
      ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 3);
  }());
}

// An empty mux_many that is drained without ever starting a slot: the first
// co_await immediately reports end() (remaining_count == 0).
TEST_F(CATEGORY, mux_many_empty_drain) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto mux = tmc::mux_many<int, 1>();
    size_t idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// A Count == 0 mux_many is a valid degenerate group with zero slots: the
// std::array storage is empty, no slot may be forked, and the first co_await
// immediately reports end(). Supporting Count == 0 keeps the template usable
// from generic code that may instantiate the zero-slot edge case (mirroring
// std::array<T, 0>). Exercises both the empty and eager constructors, and a
// void Result.
TEST_F(CATEGORY, mux_many_zero_count) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      // Empty constructor, non-void Result.
      auto mux = tmc::mux_many<int, 0>();
      size_t idx = co_await mux;
      EXPECT_EQ(idx, mux.end());
    }
    {
      // Empty constructor, void Result.
      auto mux = tmc::mux_many<void, 0>();
      size_t idx = co_await mux;
      EXPECT_EQ(idx, mux.end());
    }
    {
      // Eager constructor: the iterator produces tasks, but Count == 0 clamps
      // the group to zero slots, so nothing is initiated.
      auto iter = iter_of_static_size<5>();
      auto mux = tmc::mux_many<int, 0>(iter.begin(), iter.end());
      size_t idx = co_await mux;
      EXPECT_EQ(idx, mux.end());
    }
  }());
}

// Demonstrates the core use case: maintain a fixed level of concurrency by
// forking a slot with new work as soon as its previous result is consumed.
TEST_F(CATEGORY, mux_many_fork_maintains_concurrency) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int V) -> tmc::task<int> { co_return V; };

    auto mux = tmc::mux_many<int, 2>();
    mux.fork(0, work(0));
    mux.fork(1, work(1));

    int produced = 0;
    int sum = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      sum += mux[i];
      ++produced;
      if (produced < 4) {
        int next = (produced + 1) * 10;
        mux.fork(i, work(next));
      }
    }
    // First two results: 0 + 1. Then forks produced for produced in {1,2,3}:
    // values 20, 30, 40. 0+1+20+30+40 = 91.
    EXPECT_EQ(produced, 5);
    EXPECT_EQ(sum, 0 + 1 + 20 + 30 + 40);
  }());
}

// fork() accepts an optional executor and priority used to dispatch the
// awaitable. Here both are given explicitly (the fixture's own executor at
// priority 0); this exercises the explicit-argument overload on every executor
// type the fixture set runs under. (Cross-executor and cross-priority dispatch
// is verified more thoroughly in test_prio.cpp.)
TEST_F(CATEGORY, mux_many_fork_explicit_executor_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto work = [](int V) -> tmc::task<int> { co_return V; };
    auto mux = tmc::mux_many<int, 2>();
    mux.fork(0, work(1), ex(), 0);
    mux.fork(1, work(2), ex(), 0);

    int sum = 0;
    int count = 0;
    for (size_t i = co_await mux; i != mux.end(); i = co_await mux) {
      sum += mux[i];
      ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 3);
  }());
}

/*** fork(idx, ...) value categories and awaitable modes ***/

// Mode TMC_TASK, movable rvalue replacement. The replacement tmc::task is a
// prvalue, so it is moved into fork().
TEST_F(CATEGORY, mux_many_fork_task_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 0;
    };

    std::array<tmc::task<int>, 2> tasks{immediate(1), blocker(block)};
    auto mux = tmc::mux_many<int, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 1);

    mux.fork(0, immediate(4)); // TMC_TASK, movable rvalue
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 4);

    block.inc(); // release slot 1
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode TMC_TASK, void result. operator[] is a no-op.
TEST_F(CATEGORY, mux_many_fork_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::array<tmc::task<void>, 2> tasks{immediate(), blocker(block)};
    auto mux = tmc::mux_many<void, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    mux[0]; // void no-op

    mux.fork(0, immediate()); // TMC_TASK, void
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    mux[0];

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode TMC_TASK, non-default-constructible result. The slot is wrapped in a
// std::optional, exercising fork()'s ResultStorage match (the static_assert)
// and the optional-wrapped slot.
TEST_F(CATEGORY, mux_many_fork_non_default_constructible) {
  struct NoDefault {
    int v;
    NoDefault() = delete;
    explicit NoDefault(int V) : v(V) {}
  };
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<NoDefault> { co_return NoDefault{V}; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<NoDefault> {
      co_await B;
      co_return NoDefault{0};
    };

    std::array<tmc::task<NoDefault>, 2> tasks{immediate(7), blocker(block)};
    auto mux = tmc::mux_many<NoDefault, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    // mux[0] unwraps the std::optional and returns NoDefault&.
    EXPECT_EQ(mux[0].v, 7);

    mux.fork(0, immediate(11));
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0].v, 11);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode COROUTINE, movable rvalue replacement.
TEST_F(CATEGORY, mux_many_fork_coroutine_mode) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 0;
    };

    std::array<tmc::task<int>, 2> tasks{immediate(1), blocker(block)};
    auto mux = tmc::mux_many<int, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 1);

    mux.fork(0, mux_coroutine_op<int>{immediate(4)}); // COROUTINE, rvalue
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 4);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode WRAPPER, movable rvalue replacement. safe_wrap() moves the awaitable into
// the wrapper coroutine (consume-once).
TEST_F(CATEGORY, mux_many_fork_wrapper_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 0;
    };

    std::array<tmc::task<int>, 2> tasks{immediate(1), blocker(block)};
    auto mux = tmc::mux_many<int, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 1);

    mux.fork(0, mux_wrapper_int{4}); // WRAPPER, movable rvalue
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 4);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode WRAPPER, lvalue replacement. safe_wrap() borrows the awaitable by
// reference and awaits it as an lvalue (re-awaitable); the lvalue must outlive
// the group, so it is a named local kept alive until the drain completes.
TEST_F(CATEGORY, mux_many_fork_wrapper_lvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = [](int V) -> tmc::task<int> { co_return V; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<int> {
      co_await B;
      co_return 0;
    };

    std::array<tmc::task<int>, 2> tasks{immediate(1), blocker(block)};
    auto mux = tmc::mux_many<int, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 1);

    mux_wrapper_int w{4};
    mux.fork(0, w); // WRAPPER, lvalue (borrowed by reference)
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mux[0], 4);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode ASYNC_INITIATE, lvalue-qualified (async_initiate(self_type&)). The
// replacement is borrowed as an lvalue and driven to completion via inc().
TEST_F(CATEGORY, mux_many_fork_async_initiate_lvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::array<tmc::task<void>, 2> tasks{immediate(), blocker(block)};
    auto mux = tmc::mux_many<void, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    // atomic_awaitable is an lvalue-qualified ASYNC_INITIATE awaitable (void
    // result, matching the void task it replaces).
    atomic_awaitable<int> repl(1);
    mux.fork(0, repl); // lvalue
    repl.inc();        // drive completion
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

// Mode ASYNC_INITIATE, rvalue-qualified (async_initiate(self_type&&)). The
// replacement is a non-movable, consume-once awaitable; it is passed as an
// rvalue but borrowed in place (it can't be moved) and so is a named local kept
// alive until the drain completes, driven via complete().
TEST_F(CATEGORY, mux_many_fork_async_initiate_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> block(1);
    auto immediate = []() -> tmc::task<void> { co_return; };
    auto blocker = [](atomic_awaitable<int>& B) -> tmc::task<void> { co_await B; };

    std::array<tmc::task<void>, 2> tasks{immediate(), blocker(block)};
    auto mux = tmc::mux_many<void, 2>(tasks.begin());

    size_t idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    mux_rvalue_async_op repl;     // rvalue-qualified, non-movable, void result
    mux.fork(0, std::move(repl)); // rvalue
    repl.complete();              // drive completion
    idx = co_await mux;
    EXPECT_EQ(idx, 0u);

    block.inc();
    idx = co_await mux;
    EXPECT_EQ(idx, 1u);
    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
  }());
}

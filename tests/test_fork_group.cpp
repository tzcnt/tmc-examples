#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/fork_group.hpp"
#include "tmc/spawn.hpp"
#include "tmc/task.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_fork_group

class CATEGORY : public testing::Test {
protected:
  static inline tmc::ex_cpu otherExec;
  static void SetUpTestSuite() {
    // Create two executors so we can test run_on / resume_on
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
    otherExec.set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() {
    otherExec.teardown();
    tmc::cpu_executor().teardown();
  }

  static tmc::ex_cpu& ex() { return otherExec; }
};

static tmc::task<int> task_int(int value) { co_return value; }

static tmc::task<void> task_void() { co_return; }

// Test fork_group with tmc::task<int> (TMC_TASK mode)
TEST_F(CATEGORY, with_tmc_task) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<3, int>();
    fg.fork(task_int(1));
    fg.fork(task_int(2));
    fg.fork(task_int(3));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 3);
  }());
}

// Test fork_group with tmc::task<int> using fork_clang (TMC_TASK mode)
TEST_F(CATEGORY, with_tmc_task_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<3, int>();
    co_await fg.fork_clang(task_int(1));
    co_await fg.fork_clang(task_int(2));
    co_await fg.fork_clang(task_int(3));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 3);
  }());
}

// Test fork_group with tmc::spawn() (WRAPPER mode)
TEST_F(CATEGORY, with_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<3, int>();
    fg.fork(tmc::spawn(task_int(10)));
    fg.fork(tmc::spawn(task_int(20)));
    fg.fork(tmc::spawn(task_int(30)));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
  }());
}

// Test fork_group with tmc::spawn() using fork_clang (WRAPPER mode)
TEST_F(CATEGORY, with_wrapper_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<3, int>();
    co_await fg.fork_clang(tmc::spawn(task_int(10)));
    co_await fg.fork_clang(tmc::spawn(task_int(20)));
    co_await fg.fork_clang(tmc::spawn(task_int(30)));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
  }());
}

// Test fork_group with atomic_awaitable (ASYNC_INITIATE mode)
TEST_F(CATEGORY, with_async_initiate) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group();
    atomic_awaitable<int> aa1(1);
    atomic_awaitable<int> aa2(1);
    atomic_awaitable<int> aa3(1);

    fg.fork(std::move(aa1));
    fg.fork(std::move(aa2));
    fg.fork(std::move(aa3));
    aa1.inc();
    aa2.inc();
    aa3.inc();

    co_await std::move(fg);
  }());
}

// Test fork_group with atomic_awaitable using fork_clang (ASYNC_INITIATE mode)
TEST_F(CATEGORY, with_async_initiate_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group();
    atomic_awaitable<int> aa1(1);
    atomic_awaitable<int> aa2(1);
    atomic_awaitable<int> aa3(1);

    co_await fg.fork_clang(std::move(aa1));
    co_await fg.fork_clang(std::move(aa2));
    co_await fg.fork_clang(std::move(aa3));

    aa1.inc();
    aa2.inc();
    aa3.inc();

    co_await std::move(fg);
  }());
}

// Test fork_group with void result
TEST_F(CATEGORY, void_result) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group();
    fg.fork(task_void());
    fg.fork(task_void());
    fg.fork(task_void());
    co_await std::move(fg);
  }());
}

// Test fork_group with void result using fork_clang
TEST_F(CATEGORY, void_result_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group();
    co_await fg.fork_clang(task_void());
    co_await fg.fork_clang(task_void());
    co_await fg.fork_clang(task_void());
    co_await std::move(fg);
  }());
}

// Test fork_group reset functionality
TEST_F(CATEGORY, reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<2, int>();

    // First round
    fg.fork(task_int(1));
    fg.fork(task_int(2));
    auto results1 = co_await std::move(fg);
    EXPECT_EQ(results1[0], 1);
    EXPECT_EQ(results1[1], 2);

    // Reset and second round
    fg.reset();
    fg.fork(task_int(3));
    fg.fork(task_int(4));
    auto results2 = co_await std::move(fg);
    EXPECT_EQ(results2[0], 3);
    EXPECT_EQ(results2[1], 4);
  }());
}

static tmc::task<int>
with_task_int(int value, tmc::ex_any* expectedExec, size_t expectedPrio) {
  EXPECT_EQ(tmc::current_executor(), expectedExec);
  EXPECT_EQ(tmc::current_priority(), expectedPrio);
  co_return value;
}

// Test fork_group with run_on (sized)
TEST_F(CATEGORY, with_run_on_sized) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<2, int>();
    fg.fork(
      with_task_int(5, tmc::cpu_executor().type_erased(), 0),
      tmc::cpu_executor()
    );
    fg.fork(
      with_task_int(6, tmc::cpu_executor().type_erased(), 0),
      tmc::cpu_executor()
    );
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 5);
    EXPECT_EQ(results[1], 6);
    // Outer task's executor should not have changed
    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
  }());
}

// Test fork_group with run_on (runtime size)
TEST_F(CATEGORY, with_run_on_runtime) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(2);
    fg.fork(
      with_task_int(5, tmc::cpu_executor().type_erased(), 0),
      tmc::cpu_executor()
    );
    fg.fork(
      with_task_int(6, tmc::cpu_executor().type_erased(), 0),
      tmc::cpu_executor()
    );
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 5);
    EXPECT_EQ(results[1], 6);
    // Outer task's executor should not have changed
    EXPECT_EQ(tmc::current_executor(), ex().type_erased());
  }());
}

// Test fork_group with resume_on (sized)
TEST_F(CATEGORY, with_resume_on_sized) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<2, int>();
    fg.fork(with_task_int(11, ex().type_erased(), 0));
    fg.fork(with_task_int(12, ex().type_erased(), 0));
    auto results = co_await std::move(fg).resume_on(tmc::cpu_executor());
    EXPECT_EQ(results[0], 11);
    EXPECT_EQ(results[1], 12);
    // Outer task's executor should have changed
    EXPECT_EQ(tmc::current_executor(), tmc::cpu_executor().type_erased());
  }());
}

// Test fork_group with resume_on (runtime size)
TEST_F(CATEGORY, with_resume_on_runtime) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(2);
    fg.fork(with_task_int(11, ex().type_erased(), 0));
    fg.fork(with_task_int(12, ex().type_erased(), 0));
    auto results = co_await std::move(fg).resume_on(tmc::cpu_executor());
    EXPECT_EQ(results[0], 11);
    EXPECT_EQ(results[1], 12);
    // Outer task's executor should have changed
    EXPECT_EQ(tmc::current_executor(), tmc::cpu_executor().type_erased());
  }());
}

// Test fork_group with custom priority (sized)
TEST_F(CATEGORY, with_custom_priority_sized) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<2, int>();
    fg.fork(with_task_int(7, ex().type_erased(), 1), ex(), 1);
    fg.fork(with_task_int(8, ex().type_erased(), 1), ex(), 1);
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 7);
    EXPECT_EQ(results[1], 8);
    // Outer task's priority should not have changed
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

// Test fork_group with custom priority (runtime size)
TEST_F(CATEGORY, with_custom_priority_runtime) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(2);
    fg.fork(with_task_int(7, ex().type_erased(), 1), ex(), 1);
    fg.fork(with_task_int(8, ex().type_erased(), 1), ex(), 1);
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 7);
    EXPECT_EQ(results[1], 8);
    // Outer task's priority should not have changed
    EXPECT_EQ(tmc::current_priority(), 0);
  }());
}

// Test fork_group with mixed awaitable types
TEST_F(CATEGORY, mixed_awaitables_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group();
    atomic_awaitable<int> aa(1);
    fg.fork(task_void());             // TMC_TASK
    fg.fork(tmc::spawn(task_void())); // WRAPPER
    fg.fork(std::move(aa));           // ASYNC_INITIATE
    aa.inc();
    co_await std::move(fg);
  }());
}

TEST_F(CATEGORY, mixed_awaitables_int) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<2>(task_int(1)); // TMC_TASK
    fg.fork(tmc::spawn(task_int(2)));          // WRAPPER
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
  }());
}

// Test fork_group factory function result type deduction
TEST_F(CATEGORY, factory_deduction) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Test with tmc::task<int> - should deduce Result = int
    {
      auto fg = tmc::fork_group<1>(task_int(100));
      static_assert(std::is_same_v<decltype(fg), tmc::aw_fork_group<1, int>>);
      auto results = co_await std::move(fg);
      EXPECT_EQ(results[0], 100);
    }

    // Test with tmc::task<void> - should deduce Result = void
    {
      auto fg = tmc::fork_group(task_void());
      static_assert(std::is_same_v<decltype(fg), tmc::aw_fork_group<0, void>>);
      co_await std::move(fg);
    }

    // Test with tmc::spawn(task<int>) - should deduce Result = int
    {
      auto fg = tmc::fork_group<1>(tmc::spawn(task_int(200)));
      static_assert(std::is_same_v<decltype(fg), tmc::aw_fork_group<1, int>>);
      auto results = co_await std::move(fg);
      EXPECT_EQ(results[0], 200);
    }

    // Test with tmc::spawn(task<void>) - should deduce Result = void
    {
      auto fg = tmc::fork_group(tmc::spawn(task_void()));
      static_assert(std::is_same_v<decltype(fg), tmc::aw_fork_group<0, void>>);
      co_await std::move(fg);
    }

    // Test with atomic_awaitable - should deduce Result = void
    {
      atomic_awaitable<int> aa(1);
      auto fg = tmc::fork_group(std::move(aa));
      static_assert(std::is_same_v<decltype(fg), tmc::aw_fork_group<0, void>>);
      aa.inc();
      co_await std::move(fg);
    }
  }());
}

// Test fork_group with MaxCount=3 but only 1 awaitable submitted
TEST_F(CATEGORY, fork_group_partial_fill) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<3, int>();
    fg.fork(task_int(5));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 5);
  }());
}

// Test fork_group with RuntimeMaxCount and no initial awaitable
TEST_F(CATEGORY, runtime_maxcount) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(3);
    fg.fork(task_int(10));
    fg.fork(task_int(20));
    fg.fork(task_int(30));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
  }());
}

// Test fork_group with RuntimeMaxCount and initial awaitable
TEST_F(CATEGORY, runtime_maxcount_with_awaitable) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group(3, task_int(100));
    fg.fork(task_int(200));
    fg.fork(task_int(300));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 100);
    EXPECT_EQ(results[1], 200);
    EXPECT_EQ(results[2], 300);
  }());
}

// Test fork_group with RuntimeMaxCount and WRAPPER type
TEST_F(CATEGORY, runtime_maxcount_with_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group(2, tmc::spawn(task_int(50)));
    fg.fork(tmc::spawn(task_int(60)));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 50);
    EXPECT_EQ(results[1], 60);
  }());
}

// Test fork_group with RuntimeMaxCount partial fill
TEST_F(CATEGORY, runtime_maxcount_partial_fill) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(5);
    fg.fork(task_int(1));
    fg.fork(task_int(2));
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
  }());
}

// Test fork_group with RuntimeMaxCount and mixed awaitable types
TEST_F(CATEGORY, mixed_awaitables_int_dynamic) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group(2, task_int(1)); // TMC_TASK
    fg.fork(tmc::spawn(task_int(2)));          // WRAPPER
    auto results = co_await std::move(fg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
  }());
}

// Test fork_group with RuntimeMaxCount and reset
TEST_F(CATEGORY, runtime_maxcount_reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(2);

    // First round
    fg.fork(task_int(10));
    fg.fork(task_int(20));
    auto results1 = co_await std::move(fg);
    EXPECT_EQ(results1[0], 10);
    EXPECT_EQ(results1[1], 20);

    // Reset with a different size and second round
    fg.reset(3);
    fg.fork(task_int(30));
    fg.fork(task_int(40));
    fg.fork(task_int(50));
    auto results2 = co_await std::move(fg);
    EXPECT_EQ(results2[0], 30);
    EXPECT_EQ(results2[1], 40);
    EXPECT_EQ(results2[2], 50);
  }());
}

// Test fork_group with no awaitables (void result)
TEST_F(CATEGORY, empty_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group();
    co_await std::move(fg);
  }());
}

// Test fork_group with no awaitables (fixed-size array)
TEST_F(CATEGORY, empty_fixed_size) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<3, int>();
    [[maybe_unused]] auto results = co_await std::move(fg);
  }());
}

// Test fork_group with no awaitables (runtime size)
TEST_F(CATEGORY, empty_runtime_size) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(3);
    auto results = co_await std::move(fg);
  }());
}

// Test fork_group capacity() with fixed size
TEST_F(CATEGORY, capacity_fixed_size) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<5, int>();
    EXPECT_EQ(fg.capacity(), 5);
    fg.fork(task_int(1));
    EXPECT_EQ(fg.capacity(), 5);
    fg.fork(task_int(2));
    EXPECT_EQ(fg.capacity(), 5);
    [[maybe_unused]] auto results = co_await std::move(fg);
    EXPECT_EQ(fg.capacity(), 5);
  }());
}

// Test fork_group capacity() with runtime size
TEST_F(CATEGORY, capacity_runtime_size) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(7);
    EXPECT_EQ(fg.capacity(), 7);
    fg.fork(task_int(1));
    EXPECT_EQ(fg.capacity(), 7);
    auto results = co_await std::move(fg);
  }());
}

// Test fork_group capacity() with void result (unlimited)
TEST_F(CATEGORY, capacity_void_unlimited) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group();
    EXPECT_EQ(fg.capacity(), static_cast<size_t>(-1));
    fg.fork(task_void());
    fg.fork(task_void());
    EXPECT_EQ(fg.capacity(), static_cast<size_t>(-1));
    co_await std::move(fg);
  }());
}

// Test fork_group size() with reset
TEST_F(CATEGORY, size_after_reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<2, int>();
    EXPECT_EQ(fg.size(), 0);
    fg.fork(task_int(1));
    fg.fork(task_int(2));
    EXPECT_EQ(fg.size(), 2);
    [[maybe_unused]] auto results1 = co_await std::move(fg);

    fg.reset();
    EXPECT_EQ(fg.size(), 0);
    fg.fork(task_int(3));
    EXPECT_EQ(fg.size(), 1);
    [[maybe_unused]] auto results2 = co_await std::move(fg);
  }());
}

// Test fork_group size() with reset with constructor init
TEST_F(CATEGORY, size_after_reset_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<2>(task_int(1));
    EXPECT_EQ(fg.size(), 1);
    fg.fork(task_int(2));
    EXPECT_EQ(fg.size(), 2);
    [[maybe_unused]] auto results1 = co_await std::move(fg);

    fg.reset();
    EXPECT_EQ(fg.size(), 0);
    fg.fork(task_int(3));
    EXPECT_EQ(fg.size(), 1);
    [[maybe_unused]] auto results2 = co_await std::move(fg);
  }());
}

// Test fork_group size() with runtime size
TEST_F(CATEGORY, size_after_reset_dynamic) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group<int>(2);
    EXPECT_EQ(fg.size(), 0);
    EXPECT_EQ(fg.capacity(), 2);
    fg.fork(task_int(10));
    fg.fork(task_int(20));
    EXPECT_EQ(fg.size(), 2);
    auto results = co_await std::move(fg);

    fg.reset(4);
    EXPECT_EQ(fg.size(), 0);
    EXPECT_EQ(fg.capacity(), 4);
    fg.fork(task_int(3));
    EXPECT_EQ(fg.size(), 1);
    auto results2 = co_await std::move(fg);
  }());
}

// Test fork_group size() with runtime size + constructor init
TEST_F(CATEGORY, size_after_reset_dynamic_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto fg = tmc::fork_group(2, task_int(10));
    EXPECT_EQ(fg.size(), 1);
    EXPECT_EQ(fg.capacity(), 2);
    fg.fork(task_int(20));
    EXPECT_EQ(fg.size(), 2);
    auto results = co_await std::move(fg);

    fg.reset(4);
    EXPECT_EQ(fg.size(), 0);
    EXPECT_EQ(fg.capacity(), 4);
    fg.fork(task_int(3));
    EXPECT_EQ(fg.size(), 1);
    auto results2 = co_await std::move(fg);
  }());
}

#include "test_common.hpp"
#include "tmc/spawn.hpp"
#include "tmc/spawn_group.hpp"
#include "tmc/task.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_spawn_group

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).set_priority_count(2).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static tmc::task<int> task_int(int value) { co_return value; }

static tmc::task<void> task_void() { co_return; }

// Test spawn_group with tmc::task<int> (TMC_TASK mode)
TEST_F(CATEGORY, with_tmc_task) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<3>(task_int(1));
    sg.add(task_int(2));
    co_await sg.add_clang(task_int(3));
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 3);
  }());
}

// Test spawn_group with tmc::spawn() (WRAPPER mode)
TEST_F(CATEGORY, with_wrapper) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group(tmc::spawn(task_int(10)));
    sg.add(tmc::spawn(task_int(20)));
    co_await sg.add_clang(tmc::spawn(task_int(30)));
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);
    EXPECT_EQ(results[2], 30);
  }());
}

// Test spawn_group with void result
TEST_F(CATEGORY, void_result) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group(task_void());
    sg.add(task_void());
    co_await sg.add_clang(task_void());
    co_await std::move(sg);
  }());
}

// Test spawn_group with void result using add_clang
TEST_F(CATEGORY, void_result_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group(task_void());
    sg.add(task_void());
    co_await sg.add_clang(task_void());
    co_await std::move(sg);
  }());
}

// Test spawn_group reset functionality
TEST_F(CATEGORY, reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<2>(task_int(1));
    sg.add(task_int(2));
    auto results1 = co_await std::move(sg);
    EXPECT_EQ(results1[0], 1);
    EXPECT_EQ(results1[1], 2);

    // Reset and second round
    sg.reset();
    sg.add(task_int(3));
    sg.add(task_int(4));
    auto results2 = co_await std::move(sg);
    EXPECT_EQ(results2[0], 3);
    EXPECT_EQ(results2[1], 4);
  }());
}

// Test spawn_group with run_on
TEST_F(CATEGORY, with_run_on) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<2>(task_int(5));
    sg.add(task_int(6));
    auto results = co_await std::move(sg).run_on(tmc::cpu_executor());
    EXPECT_EQ(results[0], 5);
    EXPECT_EQ(results[1], 6);
  }());
}

// Test spawn_group with resume_on
TEST_F(CATEGORY, with_resume_on) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<2>(task_int(11));
    sg.add(task_int(12));
    auto results = co_await std::move(sg).resume_on(tmc::cpu_executor());
    EXPECT_EQ(results[0], 11);
    EXPECT_EQ(results[1], 12);
  }());
}

// Test spawn_group with priority
TEST_F(CATEGORY, with_custom_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<2>(task_int(7));
    sg.add(task_int(8));
    auto results = co_await std::move(sg).with_priority(1);
    EXPECT_EQ(results[0], 7);
    EXPECT_EQ(results[1], 8);
  }());
}

// Test spawn_group with unknown size (MaxCount == 0) specified explicitly
TEST_F(CATEGORY, unknown_size_int_explicit) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<0, tmc::task<int>>();
    sg.add(task_int(1));
    sg.add(task_int(2));
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
  }());
}

// Test spawn_group with unknown size (MaxCount == 0) specified implicitly
TEST_F(CATEGORY, unknown_size_int_implicit) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group(task_int(1));
    sg.add(task_int(2));
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
  }());
}

// Test spawn_group factory function awaitable type deduction
TEST_F(CATEGORY, factory_deduction) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      auto sg = tmc::spawn_group<1>(task_int(100));
      static_assert(
        std::is_same_v<decltype(sg), tmc::aw_spawn_group<1, tmc::task<int>>>
      );
      auto results = co_await std::move(sg);
      EXPECT_EQ(results[0], 100);
    }
    {
      auto sg = tmc::spawn_group(task_void());
      static_assert(
        std::is_same_v<decltype(sg), tmc::aw_spawn_group<0, tmc::task<void>>>
      );
      co_await std::move(sg);
    }
    {
      auto sg = tmc::spawn_group(tmc::spawn(task_int(200)));
      auto results = co_await std::move(sg);
      EXPECT_EQ(results[0], 200);
    }
    {
      auto sg = tmc::spawn_group(tmc::spawn(task_void()));
      co_await std::move(sg);
    }
  }());
}

// Test spawn_group with Count=3 but only 1 awaitable submitted
TEST_F(CATEGORY, spawn_group_partial_fill) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<3>(task_int(5));
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 5);
  }());
}

// Test spawn_group fork
TEST_F(CATEGORY, fork) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<2>(task_int(1));
    sg.add(task_int(2));
    auto forked = std::move(sg).fork();
    auto results = co_await std::move(forked);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
  }());
}

// Test spawn_group with no awaitables (void result)
TEST_F(CATEGORY, empty_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group();
    co_await std::move(sg);
  }());
}

// Test spawn_group with no awaitables (fixed-size array)
TEST_F(CATEGORY, empty_int) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<1, tmc::task<int>>();
    co_await std::move(sg);
  }());
}

// Test spawn_group capacity() with fixed size
TEST_F(CATEGORY, capacity_fixed_size) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<5, tmc::task<int>>();
    EXPECT_EQ(sg.capacity(), 5);
    sg.add(task_int(1));
    EXPECT_EQ(sg.capacity(), 5);
    sg.add(task_int(2));
    EXPECT_EQ(sg.capacity(), 5);
    [[maybe_unused]] auto results = co_await std::move(sg);
  }());
}

// Test spawn_group capacity() with dynamic size (unlimited)
TEST_F(CATEGORY, capacity_dynamic_unlimited) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group(task_int(1));
    EXPECT_EQ(sg.capacity(), static_cast<size_t>(-1));
    sg.add(task_int(2));
    EXPECT_EQ(sg.capacity(), static_cast<size_t>(-1));
    auto results = co_await std::move(sg);
  }());
}

// Test spawn_group capacity() with void result (unlimited)
TEST_F(CATEGORY, capacity_void_unlimited) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group();
    EXPECT_EQ(sg.capacity(), static_cast<size_t>(-1));
    sg.add(task_void());
    sg.add(task_void());
    EXPECT_EQ(sg.capacity(), static_cast<size_t>(-1));
    co_await std::move(sg);
  }());
}

// Test spawn_group size() with constructor init
TEST_F(CATEGORY, size_init) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<4, tmc::task<int>>(task_int(1));
    EXPECT_EQ(sg.size(), 1);
    sg.add(task_int(2));
    EXPECT_EQ(sg.size(), 2);
    sg.add(task_int(3));
    EXPECT_EQ(sg.size(), 3);
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 3);
  }());
}

// Test spawn_group size() with constructor init, dynamic size
TEST_F(CATEGORY, size_init_dynamic) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group(task_int(1));
    EXPECT_EQ(sg.size(), 1);
    sg.add(task_int(2));
    EXPECT_EQ(sg.size(), 2);
    sg.add(task_int(3));
    EXPECT_EQ(sg.size(), 3);
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 1);
    EXPECT_EQ(results[1], 2);
    EXPECT_EQ(results[2], 3);
  }());
}

// Test spawn_group size() with reset
TEST_F(CATEGORY, size_after_reset) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group<2, tmc::task<int>>();
    EXPECT_EQ(sg.size(), 0);
    sg.add(task_int(1));
    sg.add(task_int(2));
    EXPECT_EQ(sg.size(), 2);
    [[maybe_unused]] auto results1 = co_await std::move(sg);

    sg.reset();
    EXPECT_EQ(sg.size(), 0);
    sg.add(task_int(3));
    EXPECT_EQ(sg.size(), 1);
    [[maybe_unused]] auto results2 = co_await std::move(sg);
  }());
}

// Test spawn_group size() with dynamic size
TEST_F(CATEGORY, size_after_reset_dynamic) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto sg = tmc::spawn_group(task_int(10));
    EXPECT_EQ(sg.size(), 1);
    sg.add(task_int(20));
    EXPECT_EQ(sg.size(), 2);
    auto results = co_await std::move(sg);
    EXPECT_EQ(results[0], 10);
    EXPECT_EQ(results[1], 20);

    sg.reset();
    EXPECT_EQ(sg.size(), 0);
    sg.add(task_int(3));
    EXPECT_EQ(sg.size(), 1);
    auto results2 = co_await std::move(sg);
  }());
}

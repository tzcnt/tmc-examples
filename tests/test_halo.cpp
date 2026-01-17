// Test HALO (Heap Allocation eLision Optimization) functionality
// This test verifies that coroutine allocations are properly elided when using
// functions/types decorated with TMC_CORO_AWAIT_ELIDABLE_ARGUMENT or
// TMC_CORO_AWAIT_ELIDABLE.
//
// It also demonstrates all the situations in which HALO does not work, and why.
//
// NOTE: HALO only works with Clang 20+ in Release builds.

#define TMC_IMPL

#include "test_common.hpp"
#include "tmc/current.hpp"
#include "tmc/fork_group.hpp"
#include "tmc/spawn.hpp"
#include "tmc/spawn_group.hpp"
#include "tmc/spawn_tuple.hpp"
#include "tmc/task.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_halo

#if !defined(__clang_major__)
class CATEGORY : public testing::Test {};

TEST_F(CATEGORY, preconditions) {
  FAIL() << "This test suite requires Clang 20 or newer.";
}

#elif __clang_major__ < 20
class CATEGORY : public testing::Test {};

TEST_F(CATEGORY, preconditions) {
  FAIL() << "This test suite requires Clang 20 or newer.";
}

#elif !defined(NDEBUG)
class CATEGORY : public testing::Test {};

TEST_F(CATEGORY, preconditions) {
  FAIL() << "This test suite only works in Release builds. Clang doesn't "
            "perform HALO in Debug builds.";
}

#else
class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static tmc::task<int> task_int(int value) { co_return value; }

static tmc::task<void> task_void() { co_return; }

// Test HALO with tmc::task
TEST_F(CATEGORY, task) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: This allocation can be elided as it is directly awaited
      auto result = co_await task_int(1);
      EXPECT_EQ(result, 1);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    {
      // Non-HALO: This allocation cannot be elided as it is stored in a
      // variable before being awaited
      auto t = task_int(2);
      auto result = co_await std::move(t);
      EXPECT_EQ(result, 2);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
#ifdef _MSC_VER
      // Except on Windows... where clang-cl.exe is able to elide it...
      EXPECT_EQ(alloc_count, 0);
#else
      EXPECT_EQ(alloc_count, 1);
#endif
    }
    {
      // Non-HALO: This allocation cannot be elided because the .resume_on()
      // member function call doesn't count for Clang's requirement that it be
      // an "immediate right-hand side operand to a co_await expression"... see
      // https://clang.llvm.org/docs/AttributeReference.html#coro-await-elidable
      auto result = co_await task_int(2).resume_on(tmc::current_executor());
      EXPECT_EQ(result, 2);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
#ifdef _MSC_VER
      // Except on Windows... where clang-cl.exe is able to elide it...
      EXPECT_EQ(alloc_count, 0);
#else
      EXPECT_EQ(alloc_count, 2);
#endif
    }
  }());
}

// Test HALO with tmc::spawn
// Note that this is actually kind of useless as there's no reason to just call
// tmc::spawn() with no customizations - it's the same as just awaiting
// directly. And if you do apply any customizations, it will prevent HALO.
TEST_F(CATEGORY, spawn) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: This allocation can be elided as it is directly awaited
      auto result = co_await tmc::spawn(task_int(1));
      EXPECT_EQ(result, 1);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    {
      // Non-HALO: This allocation cannot be elided as it is stored in a
      // variable before being awaited
      auto t = tmc::spawn(task_int(2));
      auto result = co_await std::move(t);
      EXPECT_EQ(result, 2);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 1);
    }
    {
      // Non-HALO: spawn() with run_on() customization - due to member function
      // call, this cannot be elided
      auto result =
        co_await tmc::spawn(task_int(1)).run_on(tmc::current_executor());
      EXPECT_EQ(result, 1);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 2);
    }
  }());
}

// Test HALO with tmc::fork_clang()
TEST_F(CATEGORY, fork_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: fork_clang() is directly awaited
      auto forked = co_await tmc::fork_clang(task_int(5));
      auto result = co_await std::move(forked);
      EXPECT_EQ(result, 5);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    {
      // Non-HALO: fork_clang() stored in variable
      auto dummy = tmc::fork_clang(task_int(6));
      auto forked = co_await std::move(dummy);
      auto result = co_await std::move(forked);
      EXPECT_EQ(result, 6);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 1);
    }
    {
      // Non-HALO equivalent: spawn().fork() - due to member function call, this
      // cannot be elided
      auto forked = tmc::spawn(task_int(7)).fork();
      auto result = co_await std::move(forked);
      EXPECT_EQ(result, 7);
      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 2);
    }
  }());
}

// Test HALO with multiple fork_clang() calls (not in a loop)
TEST_F(CATEGORY, fork_clang_multiple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: All fork_clang() calls are directly awaited
      auto forked1 = co_await tmc::fork_clang(task_int(1));
      auto forked2 = co_await tmc::fork_clang(task_int(2));
      auto forked3 = co_await tmc::fork_clang(task_int(3));

      auto result1 = co_await std::move(forked1);
      auto result2 = co_await std::move(forked2);
      auto result3 = co_await std::move(forked3);

      EXPECT_EQ(result1, 1);
      EXPECT_EQ(result2, 2);
      EXPECT_EQ(result3, 3);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    {
      // Non-HALO equivalent: spawn().fork() - due to member function call, this
      // cannot be elided
      auto forked1 = tmc::spawn(task_int(4)).fork();
      auto forked2 = tmc::spawn(task_int(5)).fork();
      auto forked3 = tmc::spawn(task_int(6)).fork();

      auto result1 = co_await std::move(forked1);
      auto result2 = co_await std::move(forked2);
      auto result3 = co_await std::move(forked3);

      EXPECT_EQ(result1, 4);
      EXPECT_EQ(result2, 5);
      EXPECT_EQ(result3, 6);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
  }());
}

// Test HALO with tmc::spawn_tuple()
TEST_F(CATEGORY, spawn_tuple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: spawn_tuple() is directly awaited
      auto results = co_await tmc::spawn_tuple(task_int(1), task_int(2));

      EXPECT_EQ(std::get<0>(results), 1);
      EXPECT_EQ(std::get<1>(results), 2);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    {
      // Partial HALO: input to spawn_tuple() stored in variable
      // Only the xvalue task (4) can be HALO'd
      auto t = task_int(3);
      auto results = co_await tmc::spawn_tuple(std::move(t), task_int(4));

      EXPECT_EQ(std::get<0>(results), 3);
      EXPECT_EQ(std::get<1>(results), 4);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 1);
    }
    {
      // Non-HALO: spawn_tuple() stored in variable
      auto spawned = tmc::spawn_tuple(task_int(5), task_int(6));
      auto results = co_await std::move(spawned);

      EXPECT_EQ(std::get<0>(results), 5);
      EXPECT_EQ(std::get<1>(results), 6);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
    {
      // Non-HALO: spawn_tuple() with run_on() customization - due to member
      // function call, this cannot be elided
      auto results = co_await tmc::spawn_tuple(task_int(7), task_int(8))
                       .run_on(tmc::current_executor());

      EXPECT_EQ(std::get<0>(results), 7);
      EXPECT_EQ(std::get<1>(results), 8);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 5);
    }
  }());
}

// Test HALO with tmc::fork_tuple_clang()
TEST_F(CATEGORY, fork_tuple_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: fork_tuple_clang() is directly awaited
      auto forked = co_await tmc::fork_tuple_clang(
        task_int(10), task_int(20), task_int(30)
      );
      auto results = co_await std::move(forked);

      EXPECT_EQ(std::get<0>(results), 10);
      EXPECT_EQ(std::get<1>(results), 20);
      EXPECT_EQ(std::get<2>(results), 30);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    {
      // Non-HALO: fork_tuple_clang() stored in variable
      auto dummy =
        tmc::fork_tuple_clang(task_int(11), task_int(21), task_int(31));
      auto forked = co_await std::move(dummy);
      auto results = co_await std::move(forked);

      EXPECT_EQ(std::get<0>(results), 11);
      EXPECT_EQ(std::get<1>(results), 21);
      EXPECT_EQ(std::get<2>(results), 31);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
    {
      // Non-HALO equivalent: spawn_tuple().fork() - due to member function
      // call, this cannot be elided
      auto forked =
        tmc::spawn_tuple(task_int(12), task_int(22), task_int(32)).fork();
      auto results = co_await std::move(forked);

      EXPECT_EQ(std::get<0>(results), 12);
      EXPECT_EQ(std::get<1>(results), 22);
      EXPECT_EQ(std::get<2>(results), 32);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 6);
    }
  }());
}

// Test HALO with tmc::fork_tuple_clang() mixed types
TEST_F(CATEGORY, fork_tuple_clang_mixed) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: fork_tuple_clang() is directly awaited
      auto forked =
        co_await tmc::fork_tuple_clang(task_int(10), task_void(), task_int(20));
      auto results = co_await std::move(forked);

      EXPECT_EQ(std::get<0>(results), 10);
      EXPECT_EQ(std::get<2>(results), 20);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    {
      // Non-HALO: fork_tuple_clang() stored in variable
      auto dummy =
        tmc::fork_tuple_clang(task_int(11), task_void(), task_int(21));
      auto forked = co_await std::move(dummy);
      auto results = co_await std::move(forked);

      EXPECT_EQ(std::get<0>(results), 11);
      EXPECT_EQ(std::get<2>(results), 21);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
    {
      // Non-HALO equivalent: spawn_tuple().fork() - due to member function
      // call, this cannot be elided
      auto forked =
        tmc::spawn_tuple(task_int(12), task_void(), task_int(22)).fork();
      auto results = co_await std::move(forked);

      EXPECT_EQ(std::get<0>(results), 12);
      EXPECT_EQ(std::get<2>(results), 22);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 6);
    }
  }());
}

// Test HALO with aw_fork_group constructor initialized with a task
TEST_F(CATEGORY, fork_group_constructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // A task passed directly to the constructor cannot be HALO'd
      auto fg = tmc::fork_group<3>(task_int(5));
      auto results = co_await std::move(fg);

      EXPECT_EQ(results[0], 5);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 1);
    }
  }());
}

// Test HALO with aw_fork_group::fork_clang()
TEST_F(CATEGORY, fork_group_fork_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: fork_clang() is directly awaited for each task
      auto fg = tmc::fork_group<3, int>();
      co_await fg.fork_clang(task_int(5));
      co_await fg.fork_clang(task_int(6));
      co_await fg.fork_clang(task_int(7));
      auto results = co_await std::move(fg);

      EXPECT_EQ(results[0], 5);
      EXPECT_EQ(results[1], 6);
      EXPECT_EQ(results[2], 7);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    // {
    //   // WARNING: fork_clang() in a loop will cause a segfault!
    //   // Clang erroneously tries to use the same allocation for all subtasks
    //   auto fg = tmc::fork_group<3, int>();
    //   for (size_t i = 0; i < fg.capacity(); i++) {
    //     co_await fg.fork_clang(task_int(i));
    //   }
    //   auto results = co_await std::move(fg);

    //   EXPECT_EQ(results[0], 0);
    //   EXPECT_EQ(results[1], 1);
    //   EXPECT_EQ(results[2], 2);

    //   size_t alloc_count = tmc::debug::get_task_alloc_count();
    //   EXPECT_EQ(alloc_count, 0);
    // }
    {
      // Non-HALO: fork() without HALO attributes
      auto fg = tmc::fork_group<3, int>();
      fg.fork(task_int(8));
      fg.fork(task_int(9));
      fg.fork(task_int(10));
      auto results = co_await std::move(fg);

      EXPECT_EQ(results[0], 8);
      EXPECT_EQ(results[1], 9);
      EXPECT_EQ(results[2], 10);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
    {
      // Non-HALO: fork() without HALO attributes in a loop works fine
      auto fg = tmc::fork_group<3, int>();
      for (size_t i = 0; i < fg.capacity(); i++) {
        fg.fork(task_int(static_cast<int>(i)));
      }
      auto results = co_await std::move(fg);

      EXPECT_EQ(results[0], 0);
      EXPECT_EQ(results[1], 1);
      EXPECT_EQ(results[2], 2);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 6);
    }
  }());
}

// Test HALO with aw_fork_group::fork_clang() void result
TEST_F(CATEGORY, fork_group_fork_clang_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: fork_clang() is directly awaited for each task
      auto fg = tmc::fork_group();
      co_await fg.fork_clang(task_void());
      co_await fg.fork_clang(task_void());
      co_await fg.fork_clang(task_void());
      co_await std::move(fg);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    // {
    //   // WARNING: fork_clang() in a loop will cause a segfault!
    //   // Clang erroneously tries to use the same allocation for all subtasks
    //   auto fg = tmc::fork_group();
    //   for (size_t i = 0; i < 3; i++) {
    //     co_await fg.fork_clang(task_void());
    //   }
    //   co_await std::move(fg);

    //   size_t alloc_count = tmc::debug::get_task_alloc_count();
    //   EXPECT_EQ(alloc_count, 0);
    // }
    {
      // Non-HALO: fork() without HALO attributes
      auto fg = tmc::fork_group();
      fg.fork(task_void());
      fg.fork(task_void());
      fg.fork(task_void());
      co_await std::move(fg);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
    {
      // Non-HALO: fork() without HALO attributes in a loop works fine
      auto fg = tmc::fork_group();
      for (size_t i = 0; i < 3; i++) {
        fg.fork(task_void());
      }
      co_await std::move(fg);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 6);
    }
  }());
}

// Volatile var prevents compiler from optimizing away the if statements.
// It will statically allocate space for each fork, even if they don't run.
static volatile bool flip;

// Test HALO with aw_fork_group::fork_clang() inside of an unpredictable
// conditional.
TEST_F(CATEGORY, fork_group_fork_clang_conditional) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: fork_clang() is directly awaited for each task
      auto fg = tmc::fork_group();
      if (flip) {
        co_await fg.fork_clang(task_void());
      }
      flip = !flip;
      if (flip) {
        co_await fg.fork_clang(task_void());
      }
      flip = !flip;
      if (flip) {
        co_await fg.fork_clang(task_void());
      }
      flip = !flip;
      if (flip) {
        co_await fg.fork_clang(task_void());
      }
      EXPECT_EQ(fg.size(), 2);
      co_await std::move(fg);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
  }());
}

// Test HALO with aw_spawn_group constructor initialized with a task
TEST_F(CATEGORY, spawn_group_constructor) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // A task passed directly to the constructor cannot be HALO'd
      auto sg = tmc::spawn_group<3>(task_int(5));
      auto results = co_await std::move(sg);

      EXPECT_EQ(results[0], 5);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
#ifdef _MSC_VER
      // Except on Windows... where clang-cl.exe is able to elide it...
      // However, this does not work if you use the constructor initialization
      // in the following test (spawn_group::add_clang()), so it may be related
      // to the presence of a single active subtask which gets completely
      // inlined?
      EXPECT_EQ(alloc_count, 0);
#else
      EXPECT_EQ(alloc_count, 1);
#endif
    }
  }());
}

// Test HALO with aw_spawn_group::add_clang()
TEST_F(CATEGORY, spawn_group_add_clang) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: add_clang() is directly awaited for each task
      auto sg = tmc::spawn_group<3, tmc::task<int>>();
      co_await sg.add_clang(task_int(11));
      co_await sg.add_clang(task_int(22));
      co_await sg.add_clang(task_int(33));
      auto results = co_await std::move(sg);

      EXPECT_EQ(results[0], 11);
      EXPECT_EQ(results[1], 22);
      EXPECT_EQ(results[2], 33);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    // {
    //   // WARNING: add_clang() in a loop will cause a segfault!
    //   // Clang erroneously tries to use the same allocation for all subtasks
    //   auto sg = tmc::spawn_group<3, tmc::task<int>>();
    //   for (size_t i = 0; i < sg.capacity(); i++) {
    //     co_await sg.add_clang(task_int(i));
    //   }
    //   auto results = co_await std::move(sg);

    //   EXPECT_EQ(results[0], 0);
    //   EXPECT_EQ(results[1], 1);
    //   EXPECT_EQ(results[2], 2);

    //   size_t alloc_count = tmc::debug::get_task_alloc_count();
    //   EXPECT_EQ(alloc_count, 0);
    // }
    {
      // Non-HALO: add() without HALO attributes
      auto sg = tmc::spawn_group<3, tmc::task<int>>();
      sg.add(task_int(44));
      sg.add(task_int(55));
      sg.add(task_int(66));
      auto results = co_await std::move(sg);

      EXPECT_EQ(results[0], 44);
      EXPECT_EQ(results[1], 55);
      EXPECT_EQ(results[2], 66);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
    {
      // Non-HALO: add() without HALO attributes in a loop works fine
      auto sg = tmc::spawn_group<3, tmc::task<int>>();
      for (size_t i = 0; i < sg.capacity(); i++) {
        sg.add(task_int(static_cast<int>(i)));
      }
      auto results = co_await std::move(sg);

      EXPECT_EQ(results[0], 0);
      EXPECT_EQ(results[1], 1);
      EXPECT_EQ(results[2], 2);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 6);
    }
  }());
}

// Test HALO with aw_spawn_group::add_clang() void result
TEST_F(CATEGORY, spawn_group_add_clang_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::debug::set_task_alloc_count(0);
    {
      // HALO: add_clang() is directly awaited for each task
      auto sg = tmc::spawn_group();
      co_await sg.add_clang(task_void());
      co_await sg.add_clang(task_void());
      co_await sg.add_clang(task_void());
      co_await std::move(sg);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 0);
    }
    // {
    //   // WARNING: add_clang() in a loop will cause a segfault!
    //   // Clang erroneously tries to use the same allocation for all subtasks
    //   auto sg = tmc::spawn_group();
    //   for (size_t i = 0; i < 3; i++) {
    //     co_await sg.add_clang(task_void());
    //   }
    //   co_await std::move(sg);

    //   size_t alloc_count = tmc::debug::get_task_alloc_count();
    //   EXPECT_EQ(alloc_count, 0);
    // }
    {
      // Non-HALO: add() without HALO attributes
      auto sg = tmc::spawn_group();
      sg.add(task_void());
      sg.add(task_void());
      sg.add(task_void());
      co_await std::move(sg);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 3);
    }
    {
      // Non-HALO: add() without HALO attributes in a loop works fine
      auto sg = tmc::spawn_group();
      for (size_t i = 0; i < 3; i++) {
        sg.add(task_void());
      }
      co_await std::move(sg);

      size_t alloc_count = tmc::debug::get_task_alloc_count();
      EXPECT_EQ(alloc_count, 6);
    }
  }());
}

#endif // NDEBUG && __clang_major__ >= 20

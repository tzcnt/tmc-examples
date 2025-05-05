#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/current.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/ex_braid.hpp"
#include "tmc/spawn.hpp"
#include "tmc/spawn_func.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/spawn_tuple.hpp"
#include "tmc/sync.hpp"

#include <gtest/gtest.h>

// All 3 of the tests in this file produce TSan false positives in the same way:
// 1. Inner coro enter()s the nested executor (this is the "racing read" to
// TSan)
// 2. Inner coro exit()s the nested executor
// 3. Inner coro finishes
// 4. Outer coro destroys the nested executor (this is the "racing write" to
// TSan)

// Because the read and write happen on different threads, TSan sees this as a
// race. However, we know that the executor cannot be destroyed before the coro
// has exited.

// I tried a few different ways to counteract this:
// - using __tsan_acquire() / __tsan_release() annotations in enter(), exit()
//   and the executor destructor
// - using __attribute__((no_sanitize("thread")))
// - using the compile-time blacklist
// - using the runtime blacklist
// Unfortunately none of these approaches worked. Perhaps the coroutine
// nature of the functions confuses the name-matching behavior of TSan.
// For now I have simply disabled these tests under TSan.

#ifndef TSAN_ENABLED
template <typename Executor> tmc::task<size_t> bounce(Executor& Exec) {
  size_t result = 0;
  for (size_t i = 0; i < 10; ++i) {
    auto outerExec = tmc::current_executor();
    auto scope = co_await tmc::enter(Exec);
    EXPECT_EQ(tmc::current_executor(), Exec.type_erased());
    ++result;
    {
      // Re-entering / exiting the same executor should do nothing.
      auto innerScope = co_await tmc::enter(Exec);
      EXPECT_EQ(tmc::current_executor(), Exec.type_erased());
      co_await innerScope.exit();
      EXPECT_EQ(tmc::current_executor(), Exec.type_erased());
    }
    co_await scope.exit();
    EXPECT_EQ(tmc::current_executor(), outerExec);
    ++result;
  }
  co_return result;
}

TEST_F(CATEGORY, nested_ex_cpu) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 20);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, nested_ex_asio) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_asio localEx;
    localEx.init();

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 20);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, nested_ex_braid) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_braid localEx;

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 20);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, construct_braid_on_default_executor) {
  tmc::set_default_executor(ex());
  tmc::ex_braid br;
  test_async_main(br, [](tmc::ex_braid& Br) -> tmc::task<void> {
    EXPECT_EQ(tmc::detail::this_thread::executor, Br.type_erased());
    co_return;
  }(br));
  tmc::set_default_executor(nullptr);
}

TEST_F(CATEGORY, test_spawn_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    co_await tmc::spawn([](tmc::ex_any* Ex) -> tmc::task<void> {
      EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
      co_return;
    }(localEx.type_erased()))
      .run_on(localEx);

    EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

    co_await tmc::spawn([]() -> tmc::task<void> { co_return; }())
      .resume_on(localEx);
    EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_fork_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    {
      atomic_awaitable<size_t> aa(1);
      auto t1 =
        tmc::spawn(
          [](tmc::ex_any* Ex, atomic_awaitable<size_t>& AA) -> tmc::task<void> {
            EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
            AA.inc();
            co_return;
          }(localEx.type_erased(), aa)
        )
          .run_on(localEx)
          .fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      co_await std::move(t1);
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());
    }
    {
      atomic_awaitable<size_t> aa(1);
      auto t2 = tmc::spawn([](atomic_awaitable<size_t>& AA) -> tmc::task<void> {
                  AA.inc();
                  co_return;
                }(aa))
                  .resume_on(localEx)
                  .fork();
      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());
    }

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_func_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    co_await tmc::spawn_func([&]() -> void {
      EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());
    }).run_on(localEx);

    EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

    co_await tmc::spawn_func([]() -> void {}).resume_on(localEx);
    EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_func_fork_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    {
      atomic_awaitable<size_t> aa(1);
      auto t1 =
        tmc::spawn_func([&]() -> void {
          EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());
          aa.inc();
        })
          .run_on(localEx)
          .fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      co_await std::move(t1);

      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());
    }

    {
      atomic_awaitable<size_t> aa(1);
      auto t2 =
        tmc::spawn_func([&]() -> void { aa.inc(); }).resume_on(localEx).fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());
    }

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_tuple_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    co_await tmc::spawn_tuple([](tmc::ex_any* Ex) -> tmc::task<void> {
      EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
      co_return;
    }(localEx.type_erased()))
      .run_on(localEx);

    EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

    co_await tmc::spawn_tuple([]() -> tmc::task<void> { co_return; }())
      .resume_on(localEx);
    EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_tuple_fork_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    {
      atomic_awaitable<size_t> aa(1);
      auto t1 =
        tmc::spawn_tuple(
          [](tmc::ex_any* Ex, atomic_awaitable<size_t>& AA) -> tmc::task<void> {
            EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
            AA.inc();
            co_return;
          }(localEx.type_erased(), aa)
        )
          .run_on(localEx)
          .fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      co_await std::move(t1);

      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());
    }

    {
      atomic_awaitable<size_t> aa(1);
      auto t2 =
        tmc::spawn_tuple([](atomic_awaitable<size_t>& AA) -> tmc::task<void> {
          AA.inc();
          co_return;
        }(aa))
          .resume_on(localEx)
          .fork();
      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());
    }

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_many_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    std::array<tmc::task<void>, 1> tasks;
    tasks[0] = [](tmc::ex_any* Ex) -> tmc::task<void> {
      EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
      co_return;
    }(localEx.type_erased());

    co_await tmc::spawn_many(tasks).run_on(localEx);

    EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

    tasks[0] = []() -> tmc::task<void> { co_return; }();
    co_await tmc::spawn_many(tasks).resume_on(localEx);
    EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_many_fork_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();
    std::array<tmc::task<void>, 1> tasks;

    {
      atomic_awaitable<size_t> aa(1);
      tasks[0] =
        [](tmc::ex_any* Ex, atomic_awaitable<size_t>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
        AA.inc();
        co_return;
      }(localEx.type_erased(), aa);

      auto t1 = tmc::spawn_many(tasks).run_on(localEx).fork();
      // Ensure that the tasks complete first
      co_await aa;

      co_await std::move(t1);
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());
    }

    {
      atomic_awaitable<size_t> aa(1);
      tasks[0] = [](atomic_awaitable<size_t>& AA) -> tmc::task<void> {
        AA.inc();
        co_return;
      }(aa);
      auto t2 = tmc::spawn_many(tasks).resume_on(localEx).fork();
      // Ensure that the tasks complete first

      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());
    }

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, spawn_many_each_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();
    std::array<tmc::task<void>, 1> tasks;

    // run on localEx, resume on current ex
    {
      atomic_awaitable<size_t> aa(1);
      tasks[0] =
        [](tmc::ex_any* Ex, atomic_awaitable<size_t>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
        AA.inc();
        co_return;
      }(localEx.type_erased(), aa);

      auto t = tmc::spawn_many(tasks).run_on(localEx).result_each();
      // Ensure that the tasks complete first
      co_await aa;

      auto r = co_await t;
      EXPECT_EQ(r, 0);
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      r = co_await t;
      EXPECT_EQ(r, t.end());
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());
    }

    // run on current ex, resume on localEx
    {
      atomic_awaitable<size_t> aa(1);
      tasks[0] = [](atomic_awaitable<size_t>& AA) -> tmc::task<void> {
        AA.inc();
        co_return;
      }(aa);
      auto t = tmc::spawn_many(tasks).resume_on(localEx).result_each();
      // Ensure that the tasks complete first
      co_await aa;
      EXPECT_EQ(tmc::detail::this_thread::executor, ex().type_erased());

      auto r = co_await t;
      EXPECT_EQ(r, 0);
      EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());

      r = co_await t;
      EXPECT_EQ(r, t.end());
      EXPECT_EQ(tmc::detail::this_thread::executor, localEx.type_erased());
    }

    co_await tmc::resume_on(ex());

    co_return;
  }());
}

TEST_F(CATEGORY, post_thread_hint) {
  tmc::post_waitable(
    ex(),
    [](tmc::ex_any* Ex) -> tmc::task<void> {
      EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
      co_return;
    }(ex().type_erased()),
    0, 0
  )
    .wait();
}

TEST_F(CATEGORY, cross_post_thread_hint) {
  test_async_main(ex(), []() -> tmc::task<void> {
    atomic_awaitable<int> aa(1);
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();
    tmc::post_waitable(
      localEx,
      [](tmc::ex_any* Ex) -> tmc::task<void> {
        EXPECT_EQ(tmc::detail::this_thread::executor, Ex);
        co_return;
      }(localEx.type_erased()),
      0, 0
    )
      .wait();
    co_return;
  }());
}

#endif // TSAN_ENABLED

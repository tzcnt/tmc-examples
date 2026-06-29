#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/current.hpp"
#include "tmc/detail/concepts_awaitable.hpp"
#include "tmc/ex_braid.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/ex_cpu_st.hpp"
#include "tmc/spawn.hpp"
#include "tmc/spawn_func.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/spawn_tuple.hpp"
#include "tmc/sync.hpp"

#include <gtest/gtest.h>

template <typename Executor> tmc::task<size_t> bounce(Executor& Exec) {
  size_t result = 0;
  for (size_t i = 0; i < 10; ++i) {
    auto outerExec = tmc::current_executor();
    auto innerExec = tmc::detail::get_executor_traits<Executor>::type_erased(Exec);
    auto scope = co_await tmc::enter(Exec);
    EXPECT_EQ(tmc::current_executor(), innerExec);
    ++result;
    {
      // Re-entering / exiting the same executor should do nothing.
      auto innerScope = co_await tmc::enter(Exec);
      EXPECT_EQ(tmc::current_executor(), innerExec);
      co_await innerScope.exit();
      EXPECT_EQ(tmc::current_executor(), innerExec);
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

TEST_F(CATEGORY, nested_ex_cpu_st) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu_st localEx;
    localEx.init();

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 20);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 20);
  }());
}

#ifndef TSAN_ENABLED
// All the other TMC executors have internal protection against the race
// condition that occurs here. However we don't have control over the internals
// of the asio runloop, so it can't be fixed for ex_asio.

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

#endif

TEST_F(CATEGORY, nested_ex_braid) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_braid localEx;

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 20);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, nested_ex_any_ptr) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_any* localEx = ex().type_erased();

    auto result = co_await bounce(localEx);
    EXPECT_EQ(result, 20);
    result = co_await tmc::spawn(bounce(ex())).run_on(localEx);
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, nested_ex_any_ref) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_any* localEx = ex().type_erased();

    auto result = co_await bounce(*localEx);
    EXPECT_EQ(result, 20);
    result = co_await tmc::spawn(bounce(ex())).run_on(*localEx);
    EXPECT_EQ(result, 20);
  }());
}

TEST_F(CATEGORY, construct_braid_on_default_executor) {
  tmc::set_default_executor(ex());
  tmc::ex_braid br;
  test_async_main(br, [](tmc::ex_braid& Br) -> tmc::task<void> {
    EXPECT_EQ(tmc::current_executor(), Br.type_erased());
    co_return;
  }(br));
  tmc::set_default_executor(static_cast<tmc::ex_any*>(nullptr));
}

TEST_F(CATEGORY, test_spawn_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    co_await tmc::spawn([](tmc::ex_any* Ex) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_executor(), Ex);
      co_return;
    }(localEx.type_erased()))
      .run_on(localEx);

    EXPECT_EQ(tmc::current_executor(), ex().type_erased());

    co_await tmc::spawn([]() -> tmc::task<void> { co_return; }()).resume_on(localEx);
    EXPECT_EQ(tmc::current_executor(), localEx.type_erased());

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
        tmc::spawn([](tmc::ex_any* Ex, atomic_awaitable<size_t>& AA) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_executor(), Ex);
          AA.inc();
          co_return;
        }(localEx.type_erased(), aa))
          .run_on(localEx)
          .fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      co_await std::move(t1);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
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
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
    }

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_func_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    co_await tmc::spawn_func([&]() -> void {
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
    }).run_on(localEx);

    EXPECT_EQ(tmc::current_executor(), ex().type_erased());

    co_await tmc::spawn_func([]() -> void {}).resume_on(localEx);
    EXPECT_EQ(tmc::current_executor(), localEx.type_erased());

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_func_fork_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    {
      atomic_awaitable<size_t> aa(1);
      auto t1 = tmc::spawn_func([&]() -> void {
                  EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
                  aa.inc();
                })
                  .run_on(localEx)
                  .fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      co_await std::move(t1);

      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }

    {
      atomic_awaitable<size_t> aa(1);
      auto t2 = tmc::spawn_func([&]() -> void { aa.inc(); }).resume_on(localEx).fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
    }

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_tuple_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    co_await tmc::spawn_tuple([](tmc::ex_any* Ex) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_executor(), Ex);
      co_return;
    }(localEx.type_erased()))
      .run_on(localEx);

    EXPECT_EQ(tmc::current_executor(), ex().type_erased());

    co_await tmc::spawn_tuple([]() -> tmc::task<void> { co_return; }())
      .resume_on(localEx);
    EXPECT_EQ(tmc::current_executor(), localEx.type_erased());

    co_await tmc::resume_on(ex());
  }());
}

TEST_F(CATEGORY, test_spawn_tuple_fork_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    {
      atomic_awaitable<size_t> aa(1);
      auto t1 = tmc::spawn_tuple(
                  [](tmc::ex_any* Ex, atomic_awaitable<size_t>& AA) -> tmc::task<void> {
                    EXPECT_EQ(tmc::current_executor(), Ex);
                    AA.inc();
                    co_return;
                  }(localEx.type_erased(), aa)
      )
                  .run_on(localEx)
                  .fork();

      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      co_await std::move(t1);

      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }

    {
      atomic_awaitable<size_t> aa(1);
      auto t2 = tmc::spawn_tuple([](atomic_awaitable<size_t>& AA) -> tmc::task<void> {
                  AA.inc();
                  co_return;
                }(aa))
                  .resume_on(localEx)
                  .fork();
      // Ensure that the task completes first
      co_await aa;
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
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
      EXPECT_EQ(tmc::current_executor(), Ex);
      co_return;
    }(localEx.type_erased());

    co_await tmc::spawn_many(tasks).run_on(localEx);

    EXPECT_EQ(tmc::current_executor(), ex().type_erased());

    tasks[0] = []() -> tmc::task<void> { co_return; }();
    co_await tmc::spawn_many(tasks).resume_on(localEx);
    EXPECT_EQ(tmc::current_executor(), localEx.type_erased());

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
      tasks[0] = [](tmc::ex_any* Ex, atomic_awaitable<size_t>& AA) -> tmc::task<void> {
        EXPECT_EQ(tmc::current_executor(), Ex);
        AA.inc();
        co_return;
      }(localEx.type_erased(), aa);

      auto t1 = tmc::spawn_many(tasks).run_on(localEx).fork();
      // Ensure that the tasks complete first
      co_await aa;

      co_await std::move(t1);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
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
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      co_await std::move(t2);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
    }

    co_await tmc::resume_on(ex());
  }());
}

// mux_many captures its resume (continuation) executor at construction; it has
// no run_on()/resume_on() fluent API. fork() instead dispatches each slot to
// an explicit executor. These two blocks confirm that, wherever a slot runs, the
// consumer resumes on the executor that was current when the mux was
// constructed.
TEST_F(CATEGORY, mux_many_run_resume) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_cpu localEx;
    localEx.set_thread_count(1).init();

    // Constructed on the fixture executor; slot dispatched to localEx. The
    // consumer resumes on the fixture executor.
    {
      auto mux = tmc::mux_many<void, 1>();
      mux.fork(
        0,
        [](tmc::ex_any* Ex) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_executor(), Ex);
          co_return;
        }(localEx.type_erased()),
        localEx
      );

      auto r = co_await mux;
      EXPECT_EQ(r, 0);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());

      r = co_await mux;
      EXPECT_EQ(r, mux.end());
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }

    // Constructed while running on localEx; slot dispatched to the fixture
    // executor. The consumer resumes on localEx.
    {
      co_await tmc::resume_on(localEx);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());

      auto mux = tmc::mux_many<void, 1>();
      mux.fork(
        0,
        [](tmc::ex_any* Ex) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_executor(), Ex);
          co_return;
        }(ex().type_erased()),
        ex()
      );

      auto r = co_await mux;
      EXPECT_EQ(r, 0);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());

      r = co_await mux;
      EXPECT_EQ(r, mux.end());
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
    }

    co_await tmc::resume_on(ex());

    co_return;
  }());
}

TEST_F(CATEGORY, post_thread_hint) {
  tmc::post_waitable(
    ex(),
    [](tmc::ex_any* Ex) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_executor(), Ex);
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
        EXPECT_EQ(tmc::current_executor(), Ex);
        co_return;
      }(localEx.type_erased()),
      0, 0
    )
      .wait();
    co_return;
  }());
}

TEST_F(CATEGORY, resume_on) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).init();
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      co_await tmc::resume_on(localEx);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      co_await tmc::resume_on(ex());
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).init();
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      auto v = tmc::resume_on(localEx);
      co_await std::move(v);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      auto v2 = tmc::resume_on(ex());
      co_await std::move(v2);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }
  }());
}

TEST_F(CATEGORY, resume_on_with_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await tmc::resume_on(ex()).with_priority(1);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await tmc::resume_on(ex()).with_priority(0);
      EXPECT_EQ(tmc::current_priority(), 0);
    }
    {
      EXPECT_EQ(tmc::current_priority(), 0);
      auto v = tmc::resume_on(ex());
      co_await std::move(v).with_priority(1);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 1);
      auto v2 = tmc::resume_on(ex());
      co_await std::move(v2).with_priority(0);
      EXPECT_EQ(tmc::current_priority(), 0);
    }
  }());
}

TEST_F(CATEGORY, enter_exit) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).init();
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      auto scope = co_await tmc::enter(localEx);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      co_await scope.exit();
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).init();
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      auto v = tmc::enter(localEx);
      auto scope = co_await std::move(v);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      auto e = scope.exit();
      co_await std::move(e);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }
  }());
}

TEST_F(CATEGORY, enter_with_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).set_priority_count(2).init();
      auto scope = co_await tmc::enter(localEx).with_priority(1);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      EXPECT_EQ(tmc::current_priority(), 1);
      // After exit(), the priority should be restored to 1
      co_await scope.exit();
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 0);
    }
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).set_priority_count(2).init();
      auto v = tmc::enter(localEx);
      auto scope = co_await std::move(v).with_priority(1);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      EXPECT_EQ(tmc::current_priority(), 1);
      // After exit(), the priority should be restored to 1
      co_await scope.exit();
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 0);
    }
  }());
}

TEST_F(CATEGORY, exit_with_priority) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).set_priority_count(2).init();

      EXPECT_EQ(tmc::current_priority(), 0);
      auto scope = co_await tmc::enter(localEx);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      co_await scope.exit().with_priority(1);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await tmc::change_priority(0);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 0);
    }
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(1).set_priority_count(2).init();

      EXPECT_EQ(tmc::current_priority(), 0);
      auto scope = co_await tmc::enter(localEx);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      co_await scope.exit().with_priority(1);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 1);
      co_await tmc::change_priority(0);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
      EXPECT_EQ(tmc::current_priority(), 0);
    }
  }());
}

TEST_F(CATEGORY, exit_resume_on) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(2).init();

      tmc::ex_any* originalExec = tmc::current_executor();
      auto scope = co_await tmc::enter(localEx).with_priority(1);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      // Exit but resume on the local executor instead of original
      co_await scope.exit().resume_on(localEx);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await tmc::resume_on(originalExec);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }
    {
      tmc::ex_cpu localEx;
      localEx.set_thread_count(2).init();

      tmc::ex_any* originalExec = tmc::current_executor();
      auto v = tmc::enter(localEx);
      auto scope = co_await std::move(v).with_priority(1);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      // Exit but resume on the local executor instead of original
      co_await scope.exit().resume_on(localEx);
      EXPECT_EQ(tmc::current_executor(), localEx.type_erased());
      EXPECT_EQ(tmc::current_priority(), 0);
      co_await tmc::resume_on(originalExec);
      EXPECT_EQ(tmc::current_executor(), ex().type_erased());
    }
  }());
}

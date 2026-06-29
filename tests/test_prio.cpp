#include "test_common.hpp"
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/current.hpp"
#include "tmc/detail/concepts_awaitable.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>

#ifdef TMC_USE_BOOST_ASIO
#include <boost/asio/steady_timer.hpp>

namespace asio = boost::asio;
#else
#include <asio/steady_timer.hpp>
#endif

#include <gtest/gtest.h>

#define CATEGORY test_priority

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(2).set_priority_count(16).init();
    tmc::asio_executor().init();
  }

  static void TearDownTestSuite() {
    tmc::cpu_executor().teardown();
    tmc::asio_executor().teardown();
  }
};

constexpr size_t TASK_COUNT = 1000;

// Confirm that the current task is running on the expected
// executor and with the expected priority.
template <typename Exec>
void check_exec_prio(Exec&& ExpectedExecutor, size_t ExpectedPriority) {
  EXPECT_EQ(
    tmc::current_executor(),
    tmc::detail::get_executor_traits<Exec>::type_erased(ExpectedExecutor)
  );

  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);
}

// Enter and exit the various executors and braids running on those executors.
// Verify that the initial priority is properly captured and restored each time.
static tmc::task<void> jump_around(
  tmc::ex_braid* CpuBraid, tmc::ex_braid* AsioBraid, size_t ExpectedPriority
) {
  co_await tmc::change_priority(ExpectedPriority);
  EXPECT_EQ(
    tmc::current_executor(),
    tmc::detail::get_executor_traits<tmc::ex_cpu>::type_erased(
      tmc::cpu_executor()
    )
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  // Also test the exec_is, prio_is, and exec_prio_is detail functions
  EXPECT_TRUE(
    tmc::detail::this_thread::exec_is(
      tmc::detail::get_executor_traits<tmc::ex_cpu>::type_erased(
        tmc::cpu_executor()
      )
    )
  );
  EXPECT_TRUE(tmc::detail::this_thread::prio_is(ExpectedPriority));

  EXPECT_TRUE(
    tmc::detail::this_thread::exec_prio_is(
      tmc::detail::get_executor_traits<tmc::ex_cpu>::type_erased(
        tmc::cpu_executor()
      ),
      ExpectedPriority
    )
  );

  auto cpuBraidScope = co_await tmc::enter(CpuBraid);
  EXPECT_EQ(
    tmc::current_executor(),
    tmc::detail::get_executor_traits<tmc::ex_braid>::type_erased(*CpuBraid)
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  co_await cpuBraidScope.exit();
  EXPECT_EQ(
    tmc::current_executor(),
    tmc::detail::get_executor_traits<tmc::ex_cpu>::type_erased(
      tmc::cpu_executor()
    )
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  auto asioScope = co_await tmc::enter(tmc::asio_executor());
  EXPECT_EQ(
    tmc::current_executor(),
    tmc::detail::get_executor_traits<tmc::ex_asio>::type_erased(
      tmc::asio_executor()
    )
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  {
    auto asioBraidScope = co_await tmc::enter(AsioBraid);
    EXPECT_EQ(
      tmc::current_executor(),
      tmc::detail::get_executor_traits<tmc::ex_braid>::type_erased(*AsioBraid)
    );
    EXPECT_EQ(tmc::current_priority(), ExpectedPriority);
    co_await asioBraidScope.exit();
    check_exec_prio(tmc::asio_executor(), ExpectedPriority);
  }

  co_await asioScope.exit();
  check_exec_prio(tmc::cpu_executor(), ExpectedPriority);

  co_return;
}

TEST_F(CATEGORY, enter_exit_test) {
  tmc::async_main([]() -> tmc::task<int> {
    tmc::ex_braid cpuBraid(tmc::cpu_executor());
    tmc::ex_braid asioBraid(tmc::asio_executor());

    std::array<tmc::task<void>, TASK_COUNT> tasks;
    for (size_t i = 0; i < TASK_COUNT; ++i) {
      size_t randomPrio = static_cast<size_t>(rand()) % 16;
      tasks[i] = jump_around(&cpuBraid, &asioBraid, randomPrio);
    }

    co_await tmc::spawn_many<TASK_COUNT>(tasks.begin());
    co_return 0;
  }());
}

TEST_F(CATEGORY, braid_restore_test) {
  tmc::ex_braid br(tmc::cpu_executor());
  tmc::async_main([](tmc::ex_braid& Braid) -> tmc::task<int> {
    co_await tmc::change_priority(1);
    co_await tmc::resume_on(Braid);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::resume_on(tmc::cpu_executor());
    EXPECT_EQ(tmc::current_priority(), 1);

    co_await tmc::change_priority(2);
    co_await tmc::spawn([]() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 2);
      co_return;
    }())
      .run_on(Braid);
    EXPECT_EQ(tmc::current_priority(), 2);
    co_return 0;
  }(br));
}

TEST_F(CATEGORY, resume_on_test) {
  tmc::async_main([]() -> tmc::task<int> {
    co_await tmc::change_priority(3);
    co_await tmc::resume_on(tmc::asio_executor());
    EXPECT_EQ(tmc::current_priority(), 3);
    co_await tmc::resume_on(tmc::cpu_executor());
    EXPECT_EQ(tmc::current_priority(), 3);

    co_await tmc::change_priority(1);
    co_await tmc::spawn([]() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 1);
      co_return;
    }())
      .run_on(tmc::asio_executor());
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn([]() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 2);
      co_return;
    }())
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_with_priority_run_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn([]() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 2);
      co_return;
    }())
      .run_on(tmc::asio_executor())
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_func_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_func([]() -> void {
      EXPECT_EQ(tmc::current_priority(), 2);
    }).with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_func_with_priority_run_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_func([]() -> void {
      EXPECT_EQ(tmc::current_priority(), 2);
    })
      .run_on(tmc::asio_executor())
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_many_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [](int) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 2);
          co_return;
        }
      ),
      1
    )
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_many_with_priority_run_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_many(
      tmc::iter_adapter(
        0,
        [](int) -> tmc::task<void> {
          EXPECT_EQ(tmc::current_priority(), 2);
          co_return;
        }
      ),
      1
    )
      .run_on(tmc::asio_executor())
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

// mux_many has no with_priority()/run_on() fluent API - it captures the current
// executor and priority at construction. Constructed here at priority 2, so its
// task runs at priority 2 and the awaiting coroutine resumes at priority 2. This
// is the mux_many analogue of the spawn_many each with_priority (and
// with_priority + run_on) tests; the cross-executor resume that run_on()
// exercised is not expressible with mux_many.
TEST_F(CATEGORY, mux_many_each_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(2);
    EXPECT_EQ(tmc::current_priority(), 2);
    auto mux = tmc::mux_many<1>(tmc::iter_adapter(0, [](int) -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 2);
      co_return;
    }));
    auto v = co_await mux;
    EXPECT_EQ(v, 0);
    EXPECT_EQ(tmc::current_priority(), 2);

    v = co_await mux;
    EXPECT_EQ(v, mux.end());
    EXPECT_EQ(tmc::current_priority(), 2);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_func_many_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_func_many(
      tmc::iter_adapter(
        0,
        [](int) -> auto {
          return []() -> void { EXPECT_EQ(tmc::current_priority(), 2); };
        }
      ),
      1
    )
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_func_many_with_priority_run_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_func_many(
      tmc::iter_adapter(
        0,
        [](int) -> auto {
          return []() -> void { EXPECT_EQ(tmc::current_priority(), 2); };
        }
      ),
      1
    )
      .run_on(tmc::asio_executor())
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

// mux_many has no run_on()/with_priority() fluent API (it initiates in its
// constructor); instead it captures the current executor and priority at
// construction. Constructed here on the cpu executor at priority 2, so its tasks
// - and any restart()'d task that uses the default executor/priority - run on
// cpu@2, and the awaiting coroutine resumes on cpu@2. This is the closest
// mux_many analogue of the spawn_many result_each().replace() +
// run_on()/with_priority() test; the cross-executor resume that
// run_on()/resume_on() exercised is not expressible with mux_many.
//
// The blocked slot's task takes `gate` as a coroutine *parameter*, not a lambda
// capture. A capturing lambda coroutine invoked as a temporary leaves the lazy
// task's frame pointing at a closure that is destroyed at the end of the
// construction statement - a use-after-free of the captured reference once the
// task is resumed.
TEST_F(CATEGORY, mux_many_restart_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(2);
    EXPECT_EQ(tmc::current_priority(), 2);

    tmc::manual_reset_event gate;
    auto continuationExec = tmc::current_executor();

    std::array<tmc::task<int>, 2> tasks{
      []() -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 2);
        co_return 1;
      }(),
      [](tmc::manual_reset_event& Gate) -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 2);
        co_await Gate;
        check_exec_prio(tmc::cpu_executor(), 2);
        co_return 2;
      }(gate)};
    auto mux = tmc::mux_many<2>(tasks.begin());

    auto idx = co_await mux;
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(mux[0], 1);
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);

    mux.restart(0, []() -> tmc::task<int> {
      check_exec_prio(tmc::cpu_executor(), 2);
      co_return 4;
    }());

    idx = co_await mux;
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(mux[0], 4);
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);

    gate.set();

    idx = co_await mux;
    EXPECT_EQ(idx, 1);
    EXPECT_EQ(mux[1], 2);
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);

    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);
    co_return 0;
  }());
}

// restart() takes an optional priority used to dispatch the awaitable. Each slot
// is dispatched at an explicit priority different from the consumer's priority
// (1); confirm each awaitable runs at its requested priority while the consumer
// always resumes at its own priority on its own executor.
TEST_F(CATEGORY, mux_many_restart_custom_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    auto continuationExec = tmc::current_executor();

    auto mux = tmc::mux_many<int, 2>();

    mux.restart(
      0,
      []() -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 3);
        co_return 10;
      }(),
      tmc::cpu_executor(), 3
    );
    mux.restart(
      1,
      []() -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 5);
        co_return 20;
      }(),
      tmc::cpu_executor(), 5
    );

    int sum = 0;
    int count = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      // The dispatch priority does not affect where/when the consumer resumes:
      // it always resumes on the construction executor at its own priority.
      EXPECT_EQ(tmc::current_executor(), continuationExec);
      EXPECT_EQ(tmc::current_priority(), 1);
      sum += mux[idx];
      ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 30);
    co_return 0;
  }());
}

// restart() takes an optional executor used to dispatch the awaitable. One slot
// is dispatched on the cpu executor (the default = current) and another on the
// asio executor; confirm each runs on the requested executor while the consumer
// always resumes on the executor that was current at construction (cpu).
TEST_F(CATEGORY, mux_many_restart_custom_executor) {
  tmc::async_main([]() -> tmc::task<int> {
    co_await tmc::change_priority(1);
    auto continuationExec = tmc::current_executor(); // cpu

    auto mux = tmc::mux_many<int, 2>();

    // Default executor = current executor (cpu).
    mux.restart(0, []() -> tmc::task<int> {
      check_exec_prio(tmc::cpu_executor(), 1);
      co_return 10;
    }());
    // Explicit executor = asio; default priority = current (1).
    mux.restart(
      1,
      []() -> tmc::task<int> {
        check_exec_prio(tmc::asio_executor(), 1);
        co_return 20;
      }(),
      tmc::asio_executor()
    );

    int sum = 0;
    int count = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      EXPECT_EQ(tmc::current_executor(), continuationExec);
      EXPECT_EQ(tmc::current_priority(), 1);
      sum += mux[idx];
      ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 30);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_tuple_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_tuple([]() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 2);
      co_return;
    }())
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_tuple_with_priority_run_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_await tmc::spawn_tuple([]() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 2);
      co_return;
    }())
      .run_on(tmc::asio_executor())
      .with_priority(2);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

// mux_tuple has no with_priority()/run_on() fluent API - it captures the current
// executor and priority at construction. Constructed here at priority 2, so its
// task runs at priority 2 and the awaiting coroutine resumes at priority 2. This
// is the mux_tuple analogue of the spawn_tuple each with_priority (and
// with_priority + run_on) tests; the cross-executor resume that run_on()
// exercised is not expressible with mux_tuple.
TEST_F(CATEGORY, mux_tuple_each_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(2);
    EXPECT_EQ(tmc::current_priority(), 2);
    tmc::mux_tuple mux([]() -> tmc::task<void> {
      EXPECT_EQ(tmc::current_priority(), 2);
      co_return;
    }());
    auto v = co_await mux;
    EXPECT_EQ(v, 0);
    EXPECT_EQ(tmc::current_priority(), 2);

    v = co_await mux;
    EXPECT_EQ(v, mux.end());
    EXPECT_EQ(tmc::current_priority(), 2);
    co_return 0;
  }());
}

// mux_tuple has no run_on()/with_priority() fluent API (it initiates in its
// constructor); instead it captures the current executor and priority at
// construction. Constructed here on the cpu executor at priority 2, so its tasks
// - and any restart()'d task - run on cpu@2, and the awaiting coroutine resumes
// on cpu@2. This is the closest mux_tuple analogue of the spawn_tuple
// result_each().replace() + run_on()/with_priority() test; the cross-executor
// resume that run_on()/resume_on() exercise is not expressible with mux_tuple.
//
// The blocked slot's task takes `gate` as a coroutine *parameter*, not a lambda
// capture. A capturing lambda coroutine invoked as a temporary leaves the lazy
// task's frame pointing at a closure that is destroyed at the end of the
// construction statement - a use-after-free of the captured reference once the
// task is resumed.
TEST_F(CATEGORY, mux_tuple_restart_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(2);
    EXPECT_EQ(tmc::current_priority(), 2);

    tmc::manual_reset_event gate;
    auto continuationExec = tmc::current_executor();

    tmc::mux_tuple mux(
      []() -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 2);
        co_return 1;
      }(),
      [](tmc::manual_reset_event& Gate) -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 2);
        co_await Gate;
        check_exec_prio(tmc::cpu_executor(), 2);
        co_return 2;
      }(gate)
    );

    auto idx = co_await mux;
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(mux.get<0>(), 1);
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);

    mux.restart<0>([]() -> tmc::task<int> {
      check_exec_prio(tmc::cpu_executor(), 2);
      co_return 4;
    }());

    idx = co_await mux;
    EXPECT_EQ(idx, 0);
    EXPECT_EQ(mux.get<0>(), 4);
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);

    gate.set();

    idx = co_await mux;
    EXPECT_EQ(idx, 1);
    EXPECT_EQ(mux.get<1>(), 2);
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);

    idx = co_await mux;
    EXPECT_EQ(idx, mux.end());
    EXPECT_EQ(tmc::current_executor(), continuationExec);
    EXPECT_EQ(tmc::current_priority(), 2);
    co_return 0;
  }());
}

// restart() takes an optional priority used to dispatch the awaitable. Each slot
// is dispatched at an explicit priority different from the consumer's priority
// (1); confirm each awaitable runs at its requested priority while the consumer
// always resumes at its own priority on its own executor.
TEST_F(CATEGORY, mux_tuple_restart_custom_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    auto continuationExec = tmc::current_executor();

    tmc::mux_tuple<tmc::task<int>, tmc::task<int>> mux;

    mux.restart<0>(
      []() -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 3);
        co_return 10;
      }(),
      tmc::cpu_executor(), 3
    );
    mux.restart<1>(
      []() -> tmc::task<int> {
        check_exec_prio(tmc::cpu_executor(), 5);
        co_return 20;
      }(),
      tmc::cpu_executor(), 5
    );

    int sum = 0;
    int count = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      // The dispatch priority does not affect where/when the consumer resumes:
      // it always resumes on the construction executor at its own priority.
      EXPECT_EQ(tmc::current_executor(), continuationExec);
      EXPECT_EQ(tmc::current_priority(), 1);
      sum += (idx == 0) ? mux.get<0>() : mux.get<1>();
      ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 30);
    co_return 0;
  }());
}

// restart() takes an optional executor used to dispatch the awaitable. One slot
// is dispatched on the cpu executor (the default = current) and another on the
// asio executor; confirm each runs on the requested executor while the consumer
// always resumes on the executor that was current at construction (cpu).
TEST_F(CATEGORY, mux_tuple_restart_custom_executor) {
  tmc::async_main([]() -> tmc::task<int> {
    co_await tmc::change_priority(1);
    auto continuationExec = tmc::current_executor(); // cpu

    tmc::mux_tuple<tmc::task<int>, tmc::task<int>> mux;

    // Default executor = current executor (cpu).
    mux.restart<0>([]() -> tmc::task<int> {
      check_exec_prio(tmc::cpu_executor(), 1);
      co_return 10;
    }());
    // Explicit executor = asio; default priority = current (1).
    mux.restart<1>(
      []() -> tmc::task<int> {
        check_exec_prio(tmc::asio_executor(), 1);
        co_return 20;
      }(),
      tmc::asio_executor()
    );

    int sum = 0;
    int count = 0;
    for (auto idx = co_await mux; idx != mux.end(); idx = co_await mux) {
      EXPECT_EQ(tmc::current_executor(), continuationExec);
      EXPECT_EQ(tmc::current_priority(), 1);
      sum += (idx == 0) ? mux.get<0>() : mux.get<1>();
      ++count;
    }
    EXPECT_EQ(count, 2);
    EXPECT_EQ(sum, 30);
    co_return 0;
  }());
}

TEST_F(CATEGORY, aw_asio) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);

    auto exec = tmc::current_executor();
    co_await asio::steady_timer{
      tmc::asio_executor(), std::chrono::milliseconds(0)
    }
      .async_wait(tmc::aw_asio);
    EXPECT_EQ(tmc::current_executor(), exec);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, aw_asio_resume_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);

    co_await asio::steady_timer{
      tmc::asio_executor(), std::chrono::milliseconds(0)
    }
      .async_wait(tmc::aw_asio)
      .resume_on(tmc::asio_executor());
    EXPECT_EQ(tmc::current_executor(), tmc::asio_executor().type_erased());
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, aw_asio_spawn_tuple) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);

    auto exec = tmc::current_executor();
    co_await tmc::spawn_tuple(
      asio::steady_timer{tmc::asio_executor(), std::chrono::milliseconds(0)}
        .async_wait(tmc::aw_asio)
    );
    EXPECT_EQ(tmc::current_executor(), exec);
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, aw_asio_spawn_tuple_resume_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);

    co_await tmc::spawn_tuple(
      asio::steady_timer{tmc::asio_executor(), std::chrono::milliseconds(0)}
        .async_wait(tmc::aw_asio)
    )
      .resume_on(tmc::asio_executor());
    EXPECT_EQ(tmc::current_executor(), tmc::asio_executor().type_erased());
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

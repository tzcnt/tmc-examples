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

#include <asio/steady_timer.hpp>
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
void check_exec_prio(Exec& ExpectedExecutor, size_t ExpectedPriority) {
  EXPECT_EQ(
    tmc::detail::this_thread::executor,
    tmc::detail::executor_traits<Exec>::type_erased(ExpectedExecutor)
  );

  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);
}

// Enter and exit the various executors and braids running on those executors.
// Verify that the initial priority is properly captured and restored each time.
tmc::task<void> jump_around(
  tmc::ex_braid* CpuBraid, tmc::ex_braid* AsioBraid, size_t ExpectedPriority
) {
  co_await tmc::change_priority(ExpectedPriority);
  EXPECT_EQ(
    tmc::detail::this_thread::executor,
    tmc::detail::executor_traits<tmc::ex_cpu>::type_erased(tmc::cpu_executor())
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  auto cpuBraidScope = co_await tmc::enter(CpuBraid);
  EXPECT_EQ(
    tmc::detail::this_thread::executor,
    tmc::detail::executor_traits<tmc::ex_braid>::type_erased(*CpuBraid)
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  co_await cpuBraidScope.exit();
  EXPECT_EQ(
    tmc::detail::this_thread::executor,
    tmc::detail::executor_traits<tmc::ex_cpu>::type_erased(tmc::cpu_executor())
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  auto asioScope = co_await tmc::enter(tmc::asio_executor());
  EXPECT_EQ(
    tmc::detail::this_thread::executor,
    tmc::detail::executor_traits<tmc::ex_asio>::type_erased(tmc::asio_executor()
    )
  );
  EXPECT_EQ(tmc::current_priority(), ExpectedPriority);

  {
    auto asioBraidScope = co_await tmc::enter(AsioBraid);
    EXPECT_EQ(
      tmc::detail::this_thread::executor,
      tmc::detail::executor_traits<tmc::ex_braid>::type_erased(*AsioBraid)
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
        [](int i) -> tmc::task<void> {
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
        [](int i) -> tmc::task<void> {
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

TEST_F(CATEGORY, spawn_many_each_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    auto t = tmc::spawn_many(
               tmc::iter_adapter(
                 0,
                 [](int i) -> tmc::task<void> {
                   EXPECT_EQ(tmc::current_priority(), 2);
                   co_return;
                 }
               ),
               1
    )
               .with_priority(2)
               .result_each();
    auto v = co_await t;
    EXPECT_EQ(v, 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    v = co_await t;
    EXPECT_EQ(v, t.end());
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_many_each_with_priority_run_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    auto t = tmc::spawn_many(
               tmc::iter_adapter(
                 0,
                 [](int i) -> tmc::task<void> {
                   EXPECT_EQ(tmc::current_priority(), 2);
                   co_return;
                 }
               ),
               1
    )
               .run_on(tmc::asio_executor())
               .with_priority(2)
               .result_each();
    auto v = co_await t;
    EXPECT_EQ(v, 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    v = co_await t;
    EXPECT_EQ(v, t.end());
    EXPECT_EQ(tmc::current_priority(), 1);
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
        [](int i) -> auto {
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
        [](int i) -> auto {
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

TEST_F(CATEGORY, spawn_tuple_each_with_priority) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    auto t = tmc::spawn_tuple([]() -> tmc::task<void> {
               EXPECT_EQ(tmc::current_priority(), 2);
               co_return;
             }())
               .with_priority(2)
               .result_each();
    auto v = co_await t;
    EXPECT_EQ(v, 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    v = co_await t;
    EXPECT_EQ(v, t.end());
    EXPECT_EQ(tmc::current_priority(), 1);
    co_return 0;
  }());
}

TEST_F(CATEGORY, spawn_tuple_each_with_priority_run_on) {
  tmc::async_main([]() -> tmc::task<int> {
    EXPECT_EQ(tmc::current_priority(), 0);
    co_await tmc::change_priority(1);
    EXPECT_EQ(tmc::current_priority(), 1);
    auto t = tmc::spawn_tuple([]() -> tmc::task<void> {
               EXPECT_EQ(tmc::current_priority(), 2);
               co_return;
             }())
               .run_on(tmc::asio_executor())
               .with_priority(2)
               .result_each();
    EXPECT_EQ(tmc::current_priority(), 1);
    auto v = co_await t;
    EXPECT_EQ(v, 0);
    EXPECT_EQ(tmc::current_priority(), 1);

    v = co_await t;
    EXPECT_EQ(v, t.end());
    EXPECT_EQ(tmc::current_priority(), 1);
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

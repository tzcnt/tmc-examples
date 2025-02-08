#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"

#include <array>
#include <gtest/gtest.h>

#define CATEGORY test_priority

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(2).set_priority_count(64).init();
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
      size_t randomPrio = static_cast<size_t>(rand()) % 64;
      tasks[i] = jump_around(&cpuBraid, &asioBraid, randomPrio);
    }

    co_await tmc::spawn_many<TASK_COUNT>(tasks.begin());
    co_return 0;
  }());
}

// KNOWN ISSUE - the asio threads have their priority modified by enter()
// and don't revert it
// TEST_F(CATEGORY, resume_on_test) {
//   tmc::async_main([]() -> tmc::task<int> {
//     co_await tmc::change_priority(3);
//     co_await tmc::resume_on(tmc::asio_executor());
//     EXPECT_EQ(tmc::current_priority(), 0);
//     co_await tmc::resume_on(tmc::cpu_executor());
//     EXPECT_EQ(tmc::current_priority(), 0);
//     co_return 0;
//   }());
// }

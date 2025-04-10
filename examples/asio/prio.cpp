// Asio and braid executors don't use priority internally, but when using the
// enter() / exit() functions, the priority should be restored on the original
// executor.
#ifdef _WIN32
#include <SDKDDKVer.h>
#endif

#define TMC_IMPL

#include "tmc/asio/ex_asio.hpp"
#include "tmc/aw_resume_on.hpp"
#include "tmc/current.hpp"
#include "tmc/detail/concepts_awaitable.hpp"
#include "tmc/ex_braid.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>

constexpr size_t TASK_COUNT = 10000;

// Confirm that the current task is running on the expected
// executor and with the expected priority.
template <typename Exec>
void check_exec_prio(Exec& ExpectedExecutor, size_t ExpectedPriority) {
  if (tmc::current_executor() !=
      tmc::detail::executor_traits<Exec>::type_erased(ExpectedExecutor)) {
    std::printf("FAIL | expected executor did not match\n");
  }

  if (tmc::current_priority() != ExpectedPriority) {
    std::printf(
      "FAIL | expected priority %zu but got priority %zu\n", ExpectedPriority,
      tmc::current_priority()
    );
  }
}

// Enter and exit the various executors and braids running on those executors.
// Verify that the initial priority is properly captured and restored each time.
tmc::task<void> jump_around(
  tmc::ex_braid* CpuBraid, tmc::ex_braid* AsioBraid, size_t ExpectedPriority
) {
  co_await tmc::change_priority(ExpectedPriority);
  check_exec_prio(tmc::cpu_executor(), ExpectedPriority);

  auto cpuBraidScope = co_await tmc::enter(CpuBraid);
  check_exec_prio(*CpuBraid, ExpectedPriority);
  co_await cpuBraidScope.exit();
  check_exec_prio(tmc::cpu_executor(), ExpectedPriority);

  auto asioScope = co_await tmc::enter(tmc::asio_executor());
  check_exec_prio(tmc::asio_executor(), ExpectedPriority);
  {
    auto asioBraidScope = co_await tmc::enter(AsioBraid);
    check_exec_prio(*AsioBraid, ExpectedPriority);
    co_await asioBraidScope.exit();
    check_exec_prio(tmc::asio_executor(), ExpectedPriority);
  }

  co_await asioScope.exit();
  check_exec_prio(tmc::cpu_executor(), ExpectedPriority);

  co_return;
}

int main() {
  tmc::asio_executor().init();
  tmc::cpu_executor().set_thread_count(2).set_priority_count(64).init();
  return tmc::async_main([]() -> tmc::task<int> {
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

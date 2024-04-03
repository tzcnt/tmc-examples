// Asio and braid executors don't use priority internally, but when using the
// enter() / exit() functions, the priority should be restored on the original
// executor.
#define TMC_IMPL
#include "tmc/all_headers.hpp"
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"

#include <asio/steady_timer.hpp>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

// Confirm that the current task is running on the expected
// executor and with the expected priority.
template <tmc::detail::TypeErasableExecutor E>
void check_exec_prio(E& ExpectedExecutor, size_t ExpectedPriority) {
  auto exec = tmc::detail::this_thread::executor;
  if (exec != ExpectedExecutor.type_erased()) {
    std::printf("FAIL | expected executor did not match\n");
  }

  auto prio = tmc::detail::this_thread::this_task.prio;
  if (prio != ExpectedPriority) {
    std::printf(
      "FAIL | expected priority %" PRIu64 " but got priority %" PRIu64 "\n",
      ExpectedPriority, prio
    );
  }
}

int main() {
  tmc::asio_executor().init();
  tmc::cpu_executor().set_thread_count(1).set_priority_count(64).init();
  return tmc::async_main([]() -> tmc::task<int> {
    tmc::ex_braid cpuBraid(tmc::cpu_executor());
    tmc::ex_braid asioBraid(tmc::asio_executor());
    std::vector<tmc::aw_run_early<void, void>> runningTasks;
    for (size_t i = 0; i < 100; ++i) {
      size_t randomPrio = static_cast<size_t>(rand()) % 64;
      tmc::spawn(
        [](
          tmc::ex_braid* CpuBraid, tmc::ex_braid* AsioBraid,
          size_t ExpectedPriority
        ) -> tmc::task<void> {
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
        }(&cpuBraid, &asioBraid, randomPrio)
      )
        .with_priority(randomPrio)
        .detach(); // TODO with_priority doesn't warn (nodiscard) without detach
    }
    // TODO implement heterogenous bulk await
    // need to spawn different prios individually,
    // but bind them to the same done_count
    // For now, just wait 5 seconds as a hack job
    co_await asio::steady_timer{tmc::asio_executor(), std::chrono::seconds(5)}
      .async_wait(tmc::aw_asio);
    co_return 0;
  }());
}

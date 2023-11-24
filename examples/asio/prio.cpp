// Asio and braid executors don't use priority internally, but when using the
// enter() / exit() functions, the priority should be restored on the original
// executor.
#define TMC_IMPL

#include "tmc/all_headers.hpp"
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/detail/concepts.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/ex_cpu.hpp"
#include <atomic>
#include <cinttypes>
#include <cstdlib>
#include <iostream>

// Confirm that the current task is running on the expected
// executor and with the expected priority.
template <tmc::detail::TypeErasableExecutor E>
void check_exec_prio(E& expected_executor, size_t expected_prio) {
  auto exec = tmc::detail::this_thread::executor;
  if (exec != expected_executor.type_erased()) {
    std::printf("FAIL | expected executor did not match\n");
  }

  auto prio = tmc::detail::this_thread::this_task.prio;
  if (prio != expected_prio) {
    std::printf(
      "FAIL | expected priority %" PRIu64 " but got priority %" PRIu64 "\n",
      expected_prio, prio
    );
  }
}

int main() {
  tmc::asio_executor().init();
  tmc::cpu_executor().set_thread_count(1).set_priority_count(64).init();
  return async_main([]() -> tmc::task<int> {
    tmc::ex_braid cpu_braid(tmc::cpu_executor());
    tmc::ex_braid asio_braid(tmc::asio_executor());
    std::vector<tmc::aw_run_early<void, void>> running_tasks;
    for (size_t i = 0; i < 100; ++i) {
      size_t random_prio = static_cast<size_t>(rand()) % 64;
      tmc::spawn(
        [](
          tmc::ex_braid* cpu_br, tmc::ex_braid* asio_br, size_t expected_prio
        ) -> tmc::task<void> {
          check_exec_prio(tmc::cpu_executor(), expected_prio);

          auto braid_scope = co_await tmc::enter(cpu_br);
          check_exec_prio(*cpu_br, expected_prio);
          co_await braid_scope.exit();
          check_exec_prio(tmc::cpu_executor(), expected_prio);

          auto asio_scope = co_await tmc::enter(tmc::asio_executor());
          check_exec_prio(tmc::asio_executor(), expected_prio);
          {
            auto asio_braid_scope = co_await tmc::enter(asio_br);
            check_exec_prio(*asio_br, expected_prio);
            co_await asio_braid_scope.exit();
            check_exec_prio(tmc::asio_executor(), expected_prio);
          }

          co_await asio_scope.exit();
          check_exec_prio(tmc::cpu_executor(), expected_prio);

          co_return;
        }(&cpu_braid, &asio_braid, random_prio)
      )
        .with_priority(random_prio);
    }
    // TODO implement heterogenous bulk await
    // need to spawn different prios individually,
    // but bind them to the same done_count
    co_await asio::steady_timer{tmc::asio_executor(), std::chrono::seconds(5)}
      .async_wait(tmc::aw_asio);
    co_return 0;
  }());
}

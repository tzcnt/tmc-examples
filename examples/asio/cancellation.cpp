// Demonstrate the timer / delay capabilities of Asio executor
// Use resume_on() to send the task back and forth
// between asio_executor() and cpu_executor()

#include <asio/error.hpp>
#define TMC_IMPL

#include "asio_thread_name.hpp"

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/external.hpp"
#include "tmc/spawn_task_tuple.hpp"
#include "tmc/spawn_task_tuple_each.hpp"

#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstdio>

int main() {
  hook_init_ex_cpu_thread_name(tmc::cpu_executor());
  hook_init_ex_asio_thread_name(tmc::asio_executor());
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    print_thread_name();
    for (size_t i = 0; i < 3; ++i) {
      asio::steady_timer tim{
        tmc::asio_executor(), std::chrono::milliseconds(250)
      };
      // this demonstrates early cancellation
      auto mainTask = tmc::external::to_task<std::tuple<asio::error_code>>(
        tim.async_wait(tmc::aw_asio)
      );
      auto timeoutTask = [](asio::steady_timer& Tim) -> tmc::task<void> {
        asio::steady_timer shortTim{
          tmc::asio_executor(), std::chrono::milliseconds(100)
        };
        auto [error] = co_await shortTim.async_wait(tmc::aw_asio)
                         .resume_on(tmc::asio_executor());
        Tim.cancel();
      }(tim);
      auto waitGroup =
        tmc::spawn_tuple(std::move(mainTask), std::move(timeoutTask)).each();
      for (auto readyIdx = co_await waitGroup; readyIdx != waitGroup.end();
           readyIdx = co_await waitGroup) {
        switch (readyIdx) {
        case 0: {
          auto [ec] = waitGroup.get<0>();
          switch (ec.value()) {
          case asio::error::operation_aborted:
            std::printf("operation was canceled\n");
            break;
          case 0:
            std::printf("timer ran to completion\n");
            break;
          default:
            std::printf(
              "an unexpected error occurred: %d | %s\n", ec.value(),
              ec.message().c_str()
            );
            break;
          }
          break;
        }
        case 1: {
          std::printf("timeout occurred\n");
          tim.cancel();
          break;
        }
        }
      }
    }
    co_return 0;
  }());
}

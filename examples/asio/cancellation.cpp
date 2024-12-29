// Demonstrate how to use tmc::spawn_tuple().each() to await heterogeneous tasks
// that complete at different times. By submitting a long-running task and a
// short running timer, timeout-based cancellation can be achieved.

// The timeout nominally occurs after 100ms and the timestamps of the various
// events are logged. Note that the this example signals the cancellation event
// back to the root task by the completion of the timeout operation (at index
// 1 in the tuple), and then the root task cancels the long-running operation.
// This is somewhat less efficient than having the timeout task capture a
// reference to the long-running operation and cancel it directly after the
// timer expires.

#include <asio/error.hpp>
#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/external.hpp"
#include "tmc/spawn_task_tuple.hpp"

#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstdio>

void log_event_timestamp(
  std::string event, std::chrono::high_resolution_clock::time_point startTime,
  std::chrono::high_resolution_clock::time_point now =
    std::chrono::high_resolution_clock::now()
) {
  std::printf(
    "%s at %" PRIu64 " us\n", event.c_str(),
    std::chrono::duration_cast<std::chrono::microseconds>(now - startTime)
      .count()
  );
}

void log_error_code(
  asio::error_code ec, std::chrono::high_resolution_clock::time_point startTime
) {
  switch (ec.value()) {
  case 0:
    log_event_timestamp("operation completed", startTime);
    break;
  case asio::error::operation_aborted:
    log_event_timestamp("operation canceled", startTime);
    break;
  default:
    std::printf(
      "an unexpected error occurred: %d | %s\n", ec.value(),
      ec.message().c_str()
    );
    break;
  }
}

int main() {
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    for (size_t i = 0; i < 3; ++i) {
      auto startTime = std::chrono::high_resolution_clock::now();
      // The main task could be a network operation, but is simulated here using
      // a long-running timer
      asio::steady_timer mainOperationHandle{
        tmc::asio_executor(), std::chrono::milliseconds(1000)
      };
      auto mainTask = tmc::external::to_task<std::tuple<asio::error_code>>(
        mainOperationHandle.async_wait(tmc::aw_asio)
      );

      // A shorter timer is used for the timeout
      auto timeoutTask =
        []() -> tmc::task<std::chrono::high_resolution_clock::time_point> {
        asio::steady_timer shortTim{
          tmc::asio_executor(), std::chrono::milliseconds(100)
        };
        auto [error] = co_await shortTim.async_wait(tmc::aw_asio)
                         .resume_on(tmc::asio_executor());
        co_return std::chrono::high_resolution_clock::now();
      }();

      // Using the each() customizer allows us to receive each result
      // immediately as it becomes ready, even if the other tasks are still
      // running
      auto eachResult =
        tmc::spawn_tuple(std::move(mainTask), std::move(timeoutTask)).each();

      for (auto readyIdx = co_await eachResult; readyIdx != eachResult.end();
           readyIdx = co_await eachResult) {
        switch (readyIdx) {
        case 0: {
          // The main task is ready - check its error code to see how it
          // finished
          auto [ec] = eachResult.get<0>();
          log_error_code(ec, startTime);
          break;
        }
        case 1: {
          // The timeout task is ready; now cancel the main task
          auto timeoutAt = eachResult.get<1>();
          log_event_timestamp("timeout occurred", startTime, timeoutAt);
          mainOperationHandle.cancel();
          log_event_timestamp("cancellation signaled", startTime);
          break;
        }
        }
      }
      std::printf("\n");
    }
    co_return 0;
  }());
}

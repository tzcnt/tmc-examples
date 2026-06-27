// Demonstrate `tmc::select()`: await several operations and take the result of
// whichever one completes first, automatically cancelling the rest.
//
// This is a contrived example, since Asio operations already support native timeouts, but
// it could be used to provide a timeout cancellation with a non-Asio operation, or a
// CPU-bound operation by setting a flag inside the cancellation lambda.
//
// `tmc::select()` receives several `tmc::cancellable`, a pair type.
// Each `tmc::cancellable` holds:
// - an awaitable
// - a method to cancel the awaitable
// The `tmc::cancellable` constructor accepts flexible 2nd parameter depending on the
// cancellation method.

#ifdef _WIN32
#include <sdkddkver.h>
#endif

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/select.hpp"
#include "tmc/task.hpp"

#ifdef TMC_USE_BOOST_ASIO
#include <boost/asio/error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

namespace asio = boost::asio;
using boost::system::error_code;
#else
#include <asio/error.hpp>
#include <asio/error_code.hpp>
#include <asio/steady_timer.hpp>

using asio::error_code;
#endif

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <variant>

static void log_event_timestamp(
  std::string event, std::chrono::high_resolution_clock::time_point startTime,
  std::chrono::high_resolution_clock::time_point now =
    std::chrono::high_resolution_clock::now()
) {
  size_t duration = static_cast<size_t>(
    std::chrono::duration_cast<std::chrono::microseconds>(now - startTime).count()
  );
  std::printf("%s at %zu us\n", event.c_str(), duration);
}

static void
log_error_code(error_code ec, std::chrono::high_resolution_clock::time_point startTime) {
  switch (ec.value()) {
  case 0:
    log_event_timestamp("operation completed", startTime);
    break;
  case asio::error::operation_aborted:
    log_event_timestamp("operation canceled", startTime);
    break;
  default:
    std::printf(
      "an unexpected error occurred: %d | %s\n", ec.value(), ec.message().c_str()
    );
    break;
  }
}

int main() {
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    // The main operation could be a network operation, but is simulated here
    // using a timer. Its duration is varied across iterations so that the
    // operation wins the race in some iterations and loses (times out) in
    // others - both branches of the select are exercised.
    constexpr std::array<std::chrono::milliseconds, 3> mainOpDurations{
      std::chrono::milliseconds(50), std::chrono::milliseconds(150)
    };
    constexpr auto timeoutDuration = std::chrono::milliseconds(100);

    for (auto mainOpDuration : mainOpDurations) {
      auto startTime = std::chrono::high_resolution_clock::now();

      asio::steady_timer mainOperation{tmc::asio_executor(), mainOpDuration};
      asio::steady_timer timeout{tmc::asio_executor(), timeoutDuration};

      // Await both timers; whichever fires first wins, and the other is
      // cancelled automatically. The result variant's slots correspond to the
      // argument order: index 0 is the main operation, index 1 is the timeout.
      std::variant<std::tuple<error_code>, std::tuple<error_code>> result =
        co_await tmc::select(
          // This demonstrates 2 overloads of tmc::cancellable(), both of which are valid
          // here. #1 - 2nd parameter is a lambda that cancels the operation when invoked.
          tmc::cancellable(
            mainOperation.async_wait(tmc::aw_asio),
            [&mainOperation]() { mainOperation.cancel(); }
          ),
          // #2 - 2nd parameter is an object with a .cancel() method.
          tmc::cancellable(timeout.async_wait(tmc::aw_asio), timeout)
        );

      switch (result.index()) {
      case 0: {
        // The main operation finished first; select() cancelled the timeout.
        auto [ec] = std::get<0>(result);
        log_error_code(ec, startTime);
        break;
      }
      case 1: {
        // The timeout fired first; select() cancelled the main operation for
        // us (its discarded result would have been operation_aborted).
        log_event_timestamp("operation timed out", startTime);
        break;
      }
      default:
        // Should never happen
        exit(1);
      }
      std::printf("\n");
    }
    co_return 0;
  }());
}

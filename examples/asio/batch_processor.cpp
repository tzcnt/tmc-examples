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
#include "tmc/mux_tuple.hpp"
#include "tmc/qu_mpsc_unbounded.hpp"
#include "tmc/spawn.hpp"
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

#include <chrono>
#include <cstdio>
#include <cstdlib>

// Dummy stuff for exposition only
struct data {};
static void process(std::vector<data> batch) {
  std::printf("Processed a batch of size %zu\n", batch.size());
}
constexpr auto duration = std::chrono::milliseconds(100);

static tmc::task<void> timedBatchProcessor(tmc::qu_mpsc_unbounded<data>& q) {
  // Create a mux with 2 awaitables:
  // - a pull from a queue - starts active
  // - a timeout - starts inactive
  // Once the first item is received from the queue, initiate the timeout and continue
  // collecting data until the timeout is hit. At that point, process the batch of
  // collected data, and restart the timer upon receiving the next data element.
  tmc::mux_tuple<tmc::qu_mpsc_unbounded<data>::pull_zc_scope, std::tuple<error_code>> mux;
  mux.fork<0>(q.pull());

  asio::steady_timer timer{tmc::asio_executor()};
  std::vector<data> batch;

  while (true) {
    size_t idx = co_await mux;
    switch (idx) {
    case 0:
      if (auto& elem = mux.get<0>()) {
        // Grab the data from the queue and restart the pull awaitable
        batch.push_back(std::move(*elem));
        mux.fork<0>(q.pull());

        if (!mux.is_active<1>()) {
          // This is the first element of the batch; (re)start the timer.
          timer.expires_after(duration);
          mux.fork<1>(timer.async_wait(tmc::aw_asio));
        }
      } else {
        // Queue was closed. Cancel the timer if it's active.
        timer.cancel();
      }
      break;
    case 1:
      // Timer fired, either due to timeout or cancellation.
      // In either case, process the batch. The timer slot's bit was already
      // cleared by co_await, so is_active<1>() is now false and the next queue
      // element will restart the timer.
      process(std::move(batch));
      batch.clear();
      break;
    default:
      // idx == mux.end(); both awaitables are done and we can safely return
      co_return;
    }
  }
}

int main() {
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    tmc::qu_mpsc_unbounded<data> q;
    auto t = tmc::spawn(timedBatchProcessor(q)).fork();

    q.post(data{});
    q.post(data{});
    q.post(data{});
    q.close();

    co_await std::move(t);
    co_return 0;
  }());
}

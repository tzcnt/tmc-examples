// A simple "Hello, World!" HTTP response server
// Listens on http://localhost:55550/
#ifdef _WIN32
#include <sdkddkver.h>
#endif

#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/fork_group.hpp"
#include "tmc/spawn.hpp"
#include "tmc/task.hpp"

#ifdef TMC_USE_BOOST_ASIO
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

namespace asio = boost::asio;
using boost::system::error_code;
#else
#include <asio/buffer.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

using asio::error_code;
#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

using asio::ip::tcp;

const std::string static_response = R"(HTTP/1.1 200 OK
Content-Length: 12
Content-Type: text/plain; charset=utf-8

Hello World!)";

// not safe to accept rvalue reference
// have to accept value so that it gets moved when the coro is constructed
tmc::task<void> handler(auto Socket) {
  char data[4096];
  while (Socket.is_open()) {
    auto d = asio::buffer(data);
    auto [error, n] = co_await Socket.async_read_some(d, tmc::aw_asio);
    if (error) {
      Socket.close();
      co_return;
    }

    auto d2 = asio::buffer(static_response);
    auto [error2, n2] = co_await asio::async_write(Socket, d2, tmc::aw_asio);
    if (error2) {
      Socket.close();
      co_return;
    }
  }
  Socket.shutdown(tcp::socket::shutdown_both);
  Socket.close();
}

static tmc::task<void> accept(uint16_t Port) {
  std::printf("serving on http://localhost:%d/\n", Port);
  tcp::acceptor acceptor(tmc::asio_executor(), {tcp::v4(), Port});
  auto handlers = tmc::fork_group();
  while (true) {
    auto [error, sock] = co_await acceptor.async_accept(tmc::aw_asio);
    if (error) {
      break;
    }
    handlers.fork(handler(std::move(sock)));
  }
  // Wait for all running handlers to complete.
  co_await std::move(handlers);
}

int main() {
  tmc::cpu_executor().init();
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    auto acceptors = tmc::fork_group();
    // The default behavior is to submit each I/O call to ASIO, then resume the
    // coroutine back on tmc::cpu_executor(). Although there is additional
    // overhead with this thread transition, it may still improve overall
    // throughput since the Asio executor doesn't need to process any
    // continuations inline. Additionally, it eliminates any risk of
    // accidentally blocking the I/O thread.
    acceptors.fork(accept(55550));

    // This customization runs both the I/O calls and the continuations inline
    // on the single-threaded tmc::asio_executor(). This may or may not yield
    // higher performance for a strictly I/O latency bound benchmark such as
    // this example, and care must be taken to manually offload CPU-bound work
    // to the cpu executor.
    acceptors.fork(accept(55551), tmc::asio_executor());

    co_await std::move(acceptors);

    co_return 0;
  }());
}

// A simple "Hello, World!" HTTP response server
// Listens on http://localhost:55550/

#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task.hpp"

#include <asio/buffer.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>
#include <string>

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
  while (true) {
    auto [error, sock] = co_await acceptor.async_accept(tmc::aw_asio);
    if (error) {
      break;
    }
    tmc::spawn(handler(std::move(sock))).detach();
  }
}

int main() {
  tmc::cpu_executor().init();
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    // The default behavior is to submit each I/O call to ASIO, then resume the
    // coroutine back on tmc::cpu_executor(). This incurs additional overhead,
    // but enables unfettered access to the CPU executor without any risk of
    // accidentally blocking the I/O thread.
    tmc::spawn(accept(55550)).detach();

    // This customization runs both the I/O calls and the continuations inline
    // on the single-threaded tmc::asio_executor(). Although this yields higher
    // performance for a strictly I/O latency bound benchmark such as this
    // example, care must be taken to manually offload CPU-bound work to the cpu
    // executor.
    co_await tmc::spawn(accept(55551)).run_on(tmc::asio_executor());

    co_return 0;
  }());
}

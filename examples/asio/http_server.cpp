#include "asio/buffer.hpp"
#include "asio/use_awaitable.hpp"
#define TMC_IMPL
#include "asio/error.hpp"
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/aw_resume_on.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task.hpp"
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
tmc::task<void> handler(auto socket) {
  char data[4096];
  while (socket.is_open()) {
    auto d = asio::buffer(data);
    auto [error, n] = co_await socket.async_read_some(d, tmc::aw_asio);
    if (error) {
      socket.close();
      co_return;
    }

    auto d2 = asio::buffer(static_response);
    auto [error2, n2] = co_await asio::async_write(socket, d2, tmc::aw_asio);
    if (error2) {
      socket.close();
      co_return;
    }
  }
  socket.shutdown(tcp::socket::shutdown_both);
  socket.close();
}

int main() {
  tmc::cpu_executor().init();
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    co_await resume_on(tmc::asio_executor());
    tcp::acceptor acceptor(tmc::asio_executor(), {tcp::v4(), 55555});
    while (true) {
      auto [error, sock] = co_await acceptor.async_accept(tmc::aw_asio);
      if (error) {
        break;
      }
      spawn(handler(std::move(sock)));
    }

    co_return 0;
  }());
}
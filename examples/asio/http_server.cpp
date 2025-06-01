// A simple "Hello, World!" HTTP response server
// Listens on http://localhost:55550/
#ifdef _WIN32
#include <SDKDDKVer.h>
#endif

#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn.hpp"
#include "tmc/spawn_many.hpp"
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

static tmc::task<void> accept(tmc::ex_asio& Ex, uint16_t Port) {
  std::printf("serving on http://localhost:%d/\n", Port);
  tcp::acceptor acceptor(Ex);
  acceptor.open(tcp::v4());
  int one = 1;
  setsockopt(
    acceptor.native_handle(), SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &one,
    sizeof(one)
  );
  acceptor.bind(tcp::endpoint(tcp::v4(), Port));
  acceptor.listen();
  while (true) {
    auto [error, sock] = co_await acceptor.async_accept(tmc::aw_asio);
    if (error) {
      break;
    }
    tmc::spawn(handler(std::move(sock))).detach();
  }
}

int main(int c, char** argv) {
  tmc::cpu_executor().init();
  size_t n = 1;
#ifndef NDEBUG
#else
  if (c >= 2) {
    n = static_cast<size_t>(atoi(argv[1]));
  }
#endif

  return tmc::async_main([](int n) -> tmc::task<int> {
    tmc::detail::tiny_vec<tmc::ex_asio> execs;
    execs.resize(n);
    for (size_t i = 0; i < n; ++i) {
      execs.emplace_at(i);
      execs[i].init();
    }

    size_t i = 0;
    for (; i < n - 1; ++i) {
      tmc::spawn(accept(execs[i], 55551)).run_on(execs[i]).detach();
    }
    co_await tmc::spawn(accept(execs[i], 55551)).run_on(execs[i]);

    // co_await tmc::spawn(accept(port)).run_on(tmc::asio_executor());

    co_return 0;
  }(n));
}

// Runs the skynet benchmark on demand when the HTTP endpoint is called.
// Connections to http://localhost:55551/ will be served at lower priority
// Connections on http://localhost:55550/ will be served at higher priority
// Try load testing both sockets at the same time and observe

#define TMC_IMPL
#include "tmc/all_headers.hpp"
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include <array>
#include <asio/buffer.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/write.hpp>
#include <sstream>
#include <string>
using asio::ip::tcp;

template <size_t depth_max>
tmc::task<size_t> skynet_one(size_t base_num, size_t depth) {
  if (depth == depth_max) {
    co_return base_num;
  }
  size_t count = 0;
  size_t depth_offset = 1;
  for (size_t i = 0; i < depth_max - depth - 1; ++i) {
    depth_offset *= 10;
  }

  std::array<size_t, 10> results = co_await spawn_many<10>(tmc::iter_adapter(
    0,
    [=](size_t idx) -> tmc::task<size_t> {
      return skynet_one<depth_max>(base_num + depth_offset * idx, depth + 1);
    }
  ));

  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t depth_max> tmc::task<std::string> skynet() {
  auto start_time = std::chrono::high_resolution_clock::now();
  size_t count = co_await skynet_one<depth_max>(0, 0);
  auto end_time = std::chrono::high_resolution_clock::now();
  std::ostringstream output;
  if (count != 499999500000) {
    output << "got wrong result: " << count << "\n";
  }
  auto exec_dur = std::chrono::duration_cast<std::chrono::microseconds>(
    end_time - start_time
  );
  output << "executed skynet in " << exec_dur.count() << " us\n";
  co_return std::string(output.str());
}

// not safe to accept rvalue reference
// have to accept value so that it gets moved when the coro is constructed
tmc::task<void> handler(auto socket) {
  char data[4096];
  while (socket.is_open()) {
    // Read some request - don't care about the contents
    auto d = asio::buffer(data);
    auto [error, n] = co_await socket.async_read_some(d, tmc::aw_asio);
    if (error) {
      socket.close();
      co_return;
    }

    // Run skynet benchmark (tree of 1,111,111 subtasks)
    std::string result = co_await skynet<6>();
    asio::streambuf ostream;
    std::ostream output(&ostream);
    output << "HTTP/1.1 200 OK\nContent-Length: " << result.length()
           << "\nContent-Type: text/plain; charset=utf-8\n\n"
           << result;

    // Send response
    auto [error2, n2] =
      co_await asio::async_write(socket, ostream, tmc::aw_asio);
    if (error2) {
      socket.close();
      co_return;
    }
  }
  socket.shutdown(tcp::socket::shutdown_both);
  socket.close();
}

tmc::task<void> accept(uint16_t port) {
  tcp::acceptor acceptor(tmc::asio_executor(), {tcp::v4(), port});
  while (true) {
    auto [error, sock] = co_await acceptor.async_accept(tmc::aw_asio);
    if (error) {
      break;
    }
    spawn(handler(std::move(sock)));
  }
}

int main() {
  tmc::cpu_executor().set_priority_count(2).init();
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    std::printf("serving low priority on http://localhost::55551/\n");
    tmc::spawn(accept(55551)).with_priority(1);
    std::printf("serving high priority on http://localhost::55550/\n");
    co_await accept(55550);
    co_return 0;
  }());
}

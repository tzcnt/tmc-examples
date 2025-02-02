// Runs the skynet benchmark on demand when the HTTP endpoint is called.
// Connections to http://localhost:55551/ will be served at lower priority
// Connections on http://localhost:55550/ will be served at higher priority
// Try load testing both sockets at the same time and observe
#ifdef _WIN32
#include <SDKDDKVer.h>
#endif

#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/spawn_task.hpp"

#include <array>
#include <asio/buffer.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/signal_set.hpp>
#include <asio/streambuf.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <ranges>
#include <sstream>
#include <string>

using asio::ip::tcp;

template <size_t DepthMax>
tmc::task<size_t> skynet_one(size_t BaseNum, size_t Depth) {
  if (Depth == DepthMax) {
    co_return BaseNum;
  }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }

  std::array<size_t, 10> results = co_await tmc::spawn_many<10>(
    (std::ranges::views::iota(0UL) |
     std::ranges::views::transform([=](size_t idx) {
       return skynet_one<DepthMax>(BaseNum + depthOffset * idx, Depth + 1);
     })
    ).begin()
  );

  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t DepthMax> tmc::task<std::string> skynet() {
  auto startTime = std::chrono::high_resolution_clock::now();
  size_t count = co_await skynet_one<DepthMax>(0, 0);
  auto endTime = std::chrono::high_resolution_clock::now();
  std::ostringstream output;
  if (count != 499999500000) {
    output << "got wrong result: " << count << "\n";
  }
  auto execDur =
    std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
  output << "executed skynet in " << execDur.count() << " us\n";
  co_return std::string(output.str());
}

// not safe to accept rvalue reference
// have to accept value so that it gets moved when the coro is constructed
tmc::task<void> handler(auto Socket) {
  char data[4096];
  while (Socket.is_open()) {
    // Read some request - don't care about the contents
    auto d = asio::buffer(data);
    auto [error, n] = co_await Socket.async_read_some(d, tmc::aw_asio);
    if (error) {
      Socket.close();
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
      co_await asio::async_write(Socket, ostream, tmc::aw_asio);
    if (error2) {
      Socket.close();
      co_return;
    }
  }
  Socket.shutdown(tcp::socket::shutdown_both);
  Socket.close();
}

static tmc::task<void> accept(uint16_t Port) {
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
  tmc::cpu_executor().set_priority_count(2).init();
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    std::printf("serving low priority on http://localhost::55551/\n");
    tmc::spawn(accept(55551)).with_priority(1).detach();
    std::printf("serving high priority on http://localhost::55550/\n");
    co_await accept(55550);
    co_return 0;
  }());
}

// (no args): Create 1 I/O thread per core, bound to that core. These threads
// all listen to the same socket using SO_REUSEPORT.

// If called with '--query', returns the number of cores.
// Then it can be called N times, passing the core index as the command line
// argument each time, in parallel, to create a prefork process instead of
// prefork threads. This is what the asio_thread_per_core_prefork.sh script
// does.

#ifdef _WIN32
#include <sdkddkver.h>
#endif

#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/fork_group.hpp"
#include "tmc/sync.hpp"
#include "tmc/task.hpp"
#include "tmc/topology.hpp"

#ifdef TMC_USE_BOOST_ASIO
#include <boost/asio/basic_socket_acceptor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>

namespace asio = boost::asio;
using boost::system::error_code;
#else
#include <asio/basic_socket_acceptor.hpp>
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/write.hpp>

using asio::error_code;
#endif

#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>

using asio::ip::tcp;

#ifndef TMC_USE_HWLOC
int main() {
  std::printf("This example requires TMC_USE_HWLOC to be enabled.\n");
}
#else

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

static tmc::task<void> accept(tmc::ex_asio& ex, uint16_t Port) {
  std::printf("serving on http://localhost:%d/\n", Port);

  // Set SO_REUSEPORT to allow multiple threads to bind to the same port.
  // The OS will distribute incoming connections among our preforked workers.
  asio::basic_socket_acceptor<asio::ip::tcp, asio::io_context::executor_type>
    acceptor(ex);
  acceptor.open(tcp::v4());
  int one = 1;
#ifdef _WIN32
  setsockopt(
    acceptor.native_handle(), SOL_SOCKET, SO_REUSEADDR,
    reinterpret_cast<const char*>(&one), sizeof(one)
  );
#else
  setsockopt(
    acceptor.native_handle(), SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &one,
    sizeof(one)
  );
#endif
  acceptor.bind(tcp::endpoint(tcp::v4(), Port));
  acceptor.listen();

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

int main(int argc, char* argv[]) {
  auto topo = tmc::topology::query();
  if (argc > 1) {
    // Allow the shell to query how many cores there are for prefork
    if (0 == strcmp(argv[1], "--query")) {
      std::printf("%zu\n", topo.core_count());
      return 0;
    }

    // Create 1 single-threaded asio executor, bound to the core specified by
    // the shell. The shell will create additional processes for each core.
    size_t coreIdx = static_cast<size_t>(atoi(argv[1]));
    tmc::ex_asio ex;
    tmc::topology::topology_filter f{};
    f.set_core_indexes({coreIdx});
    ex.add_partition(f);
    ex.init();
    tmc::post_waitable(ex, accept(ex, 55550)).wait();
  } else {
    // Create 1 single-threaded asio executor per core, and bind it to that core
    // All executors live in this process

    // Executors are not movable, but they can be default-constructed, so
    // initialize the vector with the right size up front.
    std::vector<tmc::ex_asio> exs(topo.core_count());
    for (size_t i = 0; i < exs.size(); ++i) {
      tmc::topology::topology_filter f{};
      f.set_core_indexes({i});
      exs[i].add_partition(f);
      exs[i].init();
    }

    // Dispatch 1 acceptor/worker loop to each executor
    std::vector<std::future<void>> workers;
    workers.reserve(exs.size());
    for (size_t i = 0; i < exs.size(); ++i) {
      workers.emplace_back(tmc::post_waitable(exs[i], accept(exs[i], 55550)));
    }

    // Wait for all of the workers to complete.
    for (size_t i = 0; i < workers.size(); ++i) {
      workers[i].wait();
    }
  }
}

#endif

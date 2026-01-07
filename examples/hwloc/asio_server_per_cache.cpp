// (no args): For each L3 cache group (e.g. on Zen Chiplet architecture where
// there is a shared L3 cache per chiplet), create 1 I/O thread and a CPU thread
// pool bound to the same cache.

// If called with '--query', returns the number of cache groups.
// Then it can be called N times, passing the cache group index as the command
// line argument each time, in parallel, to create a prefork process instead of
// prefork threads. This is what the asio_server_per_cache_prefork.sh script
// does.

#ifdef _WIN32
#include <sdkddkver.h>
#endif

#define TMC_IMPL

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"
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

// Create a single-threaded asio executor, bound to the cache specified by
// CacheIdx. Also create a CPU thread pool, bound to the same cache.
static void configure_executors(
  tmc::ex_asio& ExAsio, tmc::ex_cpu& ExCpu, tmc::topology::cpu_topology Topo,
  size_t CacheIdx
) {
  tmc::topology::topology_filter f{};
  f.set_group_indexes({CacheIdx});

  ExAsio.add_partition(f);
  ExAsio.init();

  ExCpu.add_partition(f);
  // Create cores-1 CPU threads, leaving 1 core free for the I/O thread.
  ExCpu.set_thread_count(Topo.groups[CacheIdx].core_indexes.size() - 1);

  //// Optionally, you can increase the number of threads or use
  //// set_thread_occupancy() to make use of SMT.
  // if (topo.groups[cacheIdx].smt_level > 1) {
  //   // Use hyperthreading if available. Set occupancy just below 2, since
  //   // we need to leave room for the I/O thread.
  //   exCpu.set_thread_occupancy(1.75f);
  // }

  ExCpu.init();
}

int main(int argc, char* argv[]) {
  auto topo = tmc::topology::query();
  if (argc > 1) {
    // Allow the shell to query how many caches there are for prefork
    if (0 == strcmp(argv[1], "--query")) {
      std::printf("%zu\n", topo.groups.size());
      return 0;
    }

    // Only create 1 Asio/CPU executor pair.
    // The shell will create additional processes for each cache.
    size_t cacheIdx = static_cast<size_t>(atoi(argv[1]));
    tmc::ex_asio exAsio;
    tmc::ex_cpu exCpu;
    configure_executors(exAsio, exCpu, topo, cacheIdx);

    exCpu.init();
    // Initiate the accept loop on the CPU executor to automate CPU offloading
    tmc::post_waitable(exCpu, accept(exAsio, 55550)).wait();
  } else {
    // Create 1 single-threaded asio executor, and a CPU thread pool, per cache.
    // All executors live in this process.
    size_t cacheCount = topo.groups.size();

    // Executors are not movable, but they can be default-constructed, so
    // initialize the vectors with the right size up front.
    std::vector<tmc::ex_asio> exAsios(cacheCount);
    std::vector<tmc::ex_cpu> exCpus(cacheCount);
    for (size_t i = 0; i < cacheCount; ++i) {
      configure_executors(exAsios[i], exCpus[i], topo, i);
    }

    // Dispatch 1 acceptor/worker loop to each executor group
    std::vector<std::future<void>> workers;
    workers.reserve(cacheCount);
    for (size_t i = 0; i < cacheCount; ++i) {
      // Initiate the accept loop on the CPU executor to automate CPU
      // offloading. This CPU executor will communicate exclusively with the I/O
      // executor running on the same cache.
      workers.emplace_back(
        tmc::post_waitable(exCpus[i], accept(exAsios[i], 55550))
      );
    }

    // Wait for all of the workers to complete.
    for (size_t i = 0; i < cacheCount; ++i) {
      workers[i].wait();
    }
  }
}

#endif

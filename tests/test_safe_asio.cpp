// Tests for the tmc-asio safe_* objects (SafeAcceptor, SafeSocket, SafeTimer).
// These serialize all operation initiations on the underlying asio object, so operations
// may be initiated from different coroutines running on different executors/threads.

// Only operation initiation is serialized. This does not serialize access to any other
// variables, and it does not prevent violation of the "only 1 read + 1 write may be
// active at a time" rule. Users are still responsible for that.

#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/asio/safe_acceptor.hpp"
#include "tmc/asio/safe_socket.hpp"
#include "tmc/asio/safe_timer.hpp"

#ifdef TMC_USE_BOOST_ASIO
#include <boost/asio/ip/address.hpp>
#else
#include <asio/ip/address.hpp>
#endif

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

#define CATEGORY test_safe_asio

// The same alias the safe_* headers use to abstract over
// boost::asio / standalone asio.
namespace asio_impl = tmc::detail::asio_impl;
using tcp = tmc::SafeAcceptor::protocol_type;

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
    tmc::asio_executor().init();
  }

  static void TearDownTestSuite() {
    tmc::asio_executor().teardown();
    tmc::cpu_executor().teardown();
  }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

static tmc::SafeTimer::timer_type make_timer() {
  return tmc::SafeTimer::timer_type{tmc::asio_executor()};
}

static tmc::SafeSocket::socket_type make_socket() {
  return tmc::SafeSocket::socket_type{tmc::asio_executor()};
}

// Opens, binds, and listens on an ephemeral localhost port.
static tmc::task<tcp::endpoint> listen_local(tmc::SafeAcceptor& Acc) {
  auto ec = co_await Acc.open(tcp::v4());
  EXPECT_FALSE(ec);
  ec = co_await Acc.set_option(asio_impl::socket_base::reuse_address{true});
  EXPECT_FALSE(ec);
  ec = co_await Acc.bind({asio_impl::ip::make_address("127.0.0.1"), 0});
  EXPECT_FALSE(ec);
  ec = co_await Acc.listen();
  EXPECT_FALSE(ec);
  co_return Acc.acceptor_unsafe().local_endpoint();
}

TEST_F(CATEGORY, timer_wait) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeTimer timer{make_timer()};
    auto start = std::chrono::steady_clock::now();
    auto [ec] = co_await timer.async_wait_for(std::chrono::milliseconds(20));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_FALSE(ec);
    EXPECT_GE(elapsed, std::chrono::milliseconds(19));
  }());
}

TEST_F(CATEGORY, timer_wait_until) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeTimer timer{make_timer()};
    auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    auto [ec] = co_await timer.async_wait_until(expiry);
    EXPECT_FALSE(ec);
    EXPECT_GE(std::chrono::steady_clock::now(), expiry);
  }());
}

static tmc::task<int> timed_wait_report_abort(tmc::SafeTimer& T) {
  auto [ec] = co_await T.async_wait_for(std::chrono::milliseconds(50));
  co_return ec == asio_impl::error::operation_aborted ? 1 : 0;
}

// Two coroutines share one timer: the second async_wait_for re-arms the
// expiry, aborting the wait that was initiated first.
TEST_F(CATEGORY, timer_rearm_aborts_prior_wait) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeTimer timer{make_timer()};
    auto [a, b] = co_await tmc::spawn_tuple(
      timed_wait_report_abort(timer), timed_wait_report_abort(timer)
    );
    // Normally exactly 1. May be 0 if the first wait somehow expired before
    // the second was initiated (extremely slow machine); never 2.
    EXPECT_LE(a + b, 1);
  }());
}

static tmc::task<int> long_wait_report_abort(tmc::SafeTimer& T) {
  auto [ec] = co_await T.async_wait_for(std::chrono::seconds(60));
  co_return ec == asio_impl::error::operation_aborted ? 1 : 0;
}

TEST_F(CATEGORY, timer_cancel_count) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeTimer timer{make_timer()};
    auto waiter = tmc::spawn(long_wait_report_abort(timer)).fork();
    tmc::SafeTimer delay{make_timer()};
    std::size_t cancelled = co_await timer.cancel();
    while (cancelled == 0) {
      // The forked wait may not have been initiated yet; retry.
      co_await delay.async_wait_for(std::chrono::milliseconds(1));
      cancelled = co_await timer.cancel();
    }
    EXPECT_EQ(cancelled, 1u);
    auto aborted = co_await std::move(waiter);
    EXPECT_EQ(aborted, 1);
  }());
}

static tmc::task<void> echo_server(tmc::SafeAcceptor& Acc) {
  auto [ec, sock] = co_await Acc.async_accept();
  EXPECT_FALSE(ec);
  tmc::SafeSocket safe{std::move(sock)};
  char buf[5]{};
  auto [rec, rn] = co_await safe.async_read(asio_impl::buffer(buf, 5));
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, 5u);
  auto [wec, wn] = co_await safe.async_write(asio_impl::buffer(buf, 5));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);
  auto sec = co_await safe.shutdown_full();
  EXPECT_FALSE(sec);
}

static tmc::task<void> echo_client(tcp::endpoint Ep) {
  tmc::SafeSocket safe{make_socket()};
  auto [cec] = co_await safe.async_connect(Ep);
  EXPECT_FALSE(cec);
  char out[5] = {'h', 'e', 'l', 'l', 'o'};
  auto [wec, wn] = co_await safe.async_write(asio_impl::buffer(out, 5));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);
  char in[5]{};
  auto [rec, rn] = co_await safe.async_read(asio_impl::buffer(in, 5));
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, 5u);
  EXPECT_EQ(0, std::memcmp(out, in, 5));
  auto sec = co_await safe.shutdown_full();
  EXPECT_FALSE(sec);
}

TEST_F(CATEGORY, socket_echo) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeAcceptor acc{tmc::SafeAcceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    co_await tmc::spawn_tuple(echo_server(acc), echo_client(ep));
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

// Large enough to require many read_some/write_some chunks through the
// per-chunk mutex loop (loopback socket buffers are much smaller than this).
static constexpr std::size_t BIG_SIZE = 2 * 1024 * 1024;

static char big_pattern(std::size_t I) { return static_cast<char>(I * 31u); }

static tmc::task<void> big_server(tmc::SafeAcceptor& Acc) {
  auto [ec, sock] = co_await Acc.async_accept();
  EXPECT_FALSE(ec);
  tmc::SafeSocket safe{std::move(sock)};
  std::vector<char> data(BIG_SIZE);
  // Read into a multi-buffer sequence to exercise buffer iteration.
  std::array<asio_impl::mutable_buffer, 2> bufs{
    asio_impl::buffer(data.data(), BIG_SIZE / 2),
    asio_impl::buffer(data.data() + BIG_SIZE / 2, BIG_SIZE - BIG_SIZE / 2)
  };
  auto [rec, rn] = co_await safe.async_read(bufs);
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, BIG_SIZE);
  bool ok = true;
  for (std::size_t i = 0; i < BIG_SIZE; ++i) {
    ok = ok && (data[i] == big_pattern(i));
  }
  EXPECT_TRUE(ok);
  char ack = 'A';
  auto [wec, wn] = co_await safe.async_write(asio_impl::buffer(&ack, 1));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 1u);
  auto sec = co_await safe.shutdown_full();
  EXPECT_FALSE(sec);
}

static tmc::task<void> big_client(tcp::endpoint Ep) {
  tmc::SafeSocket safe{make_socket()};
  auto [cec] = co_await safe.async_connect(Ep);
  EXPECT_FALSE(cec);
  std::vector<char> data(BIG_SIZE);
  for (std::size_t i = 0; i < BIG_SIZE; ++i) {
    data[i] = big_pattern(i);
  }
  auto [wec, wn] = co_await safe.async_write(asio_impl::buffer(data));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, BIG_SIZE);
  char ack{};
  auto [rec, rn] = co_await safe.async_read(asio_impl::buffer(&ack, 1));
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, 1u);
  EXPECT_EQ(ack, 'A');
  auto sec = co_await safe.shutdown_full();
  EXPECT_FALSE(sec);
}

TEST_F(CATEGORY, socket_large_transfer_multibuffer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeAcceptor acc{tmc::SafeAcceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    co_await tmc::spawn_tuple(big_server(acc), big_client(ep));
    co_await acc.close();
  }());
}

static tmc::task<void> eof_server(tmc::SafeAcceptor& Acc) {
  auto [ec, sock] = co_await Acc.async_accept();
  EXPECT_FALSE(ec);
  tmc::SafeSocket safe{std::move(sock)};
  char buf[10]{};
  // The peer sends only 5 bytes and then closes; expect a partial read
  // terminated by EOF, like asio::async_read.
  auto [rec, rn] = co_await safe.async_read(asio_impl::buffer(buf));
  EXPECT_EQ(rec, asio_impl::error::eof);
  EXPECT_EQ(rn, 5u);
  co_await safe.shutdown_full();
}

static tmc::task<void> eof_client(tcp::endpoint Ep) {
  tmc::SafeSocket safe{make_socket()};
  auto [cec] = co_await safe.async_connect(Ep);
  EXPECT_FALSE(cec);
  char out[5] = {'a', 'b', 'c', 'd', 'e'};
  auto [wec, wn] = co_await safe.async_write(asio_impl::buffer(out, 5));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);
  auto sec = co_await safe.shutdown_full(); // sends FIN
  EXPECT_FALSE(sec);
}

TEST_F(CATEGORY, socket_partial_read_eof) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeAcceptor acc{tmc::SafeAcceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    co_await tmc::spawn_tuple(eof_server(acc), eof_client(ep));
    co_await acc.close();
  }());
}

static tmc::task<void> pending_reader(tmc::SafeSocket& S) {
  char buf[64]{};
  auto [ec, n] = co_await S.async_read(asio_impl::buffer(buf));
  EXPECT_TRUE(static_cast<bool>(ec));
  EXPECT_EQ(n, 0u);
}

static tmc::task<void> concurrent_closer(tmc::SafeSocket& S) {
  tmc::SafeTimer t{make_timer()};
  co_await t.async_wait_for(std::chrono::milliseconds(1));
  auto ec = co_await S.shutdown_full();
  EXPECT_FALSE(ec);
}

// shutdown_full() from one coroutine while another coroutine's async_read is
// pending on the same socket. All initiations are serialized by the internal
// mutex, so the read must fail cleanly at a chunk boundary.
TEST_F(CATEGORY, socket_shutdown_during_read) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeAcceptor acc{tmc::SafeAcceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    for (int i = 0; i < 10; ++i) {
      tmc::SafeSocket client{make_socket()};
      auto [cec] = co_await client.async_connect(ep);
      EXPECT_FALSE(cec);
      auto [aec, ssock] = co_await acc.async_accept();
      EXPECT_FALSE(aec);
      tmc::SafeSocket server{std::move(ssock)};
      co_await tmc::spawn_tuple(pending_reader(server), concurrent_closer(server));
      co_await client.shutdown_full();
    }
    co_await acc.close();
  }());
}

static tmc::task<void>
accept_expect_abort(tmc::SafeAcceptor& Acc, std::atomic<bool>& Done) {
  auto [aec, sock] = co_await Acc.async_accept();
  Done.store(true, std::memory_order_relaxed);
  EXPECT_EQ(aec, asio_impl::error::operation_aborted);
  (void)sock;
}

static tmc::task<void>
cancel_until_done(tmc::SafeAcceptor& Acc, std::atomic<bool>& Done) {
  tmc::SafeTimer delay{make_timer()};
  // Retry until the accept has actually been initiated and aborted.
  while (!Done.load(std::memory_order_relaxed)) {
    auto cec = co_await Acc.cancel();
    EXPECT_FALSE(cec);
    co_await delay.async_wait_for(std::chrono::milliseconds(1));
  }
}

// cancel() aborts a pending accept.
TEST_F(CATEGORY, acceptor_cancel_pending_accept) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::SafeAcceptor acc{tmc::SafeAcceptor::acceptor_type{tmc::asio_executor()}};
    co_await listen_local(acc);
    std::atomic<bool> done{false};
    co_await tmc::spawn_tuple(
      accept_expect_abort(acc, done), cancel_until_done(acc, done)
    );
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

#undef CATEGORY

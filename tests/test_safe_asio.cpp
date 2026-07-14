// Tests for the tmc-asio safe_* objects (asio_safe_acceptor, asio_safe_socket,
// asio_safe_timer). These serialize all operation initiations on the underlying asio
// object, so operations may be initiated from different coroutines running on different
// executors/threads.

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
namespace asio_ns = tmc::detail::asio_ns;
// Serialize the common TCP acceptor/socket. asio_safe_* is templated on the
// underlying Asio object and can wrap other protocols.
using safe_acceptor = tmc::asio_safe_acceptor<>;
using safe_socket = tmc::asio_safe_socket<>;
using tcp = safe_acceptor::protocol_type;

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

static tmc::asio_safe_timer::timer_type make_timer() {
  return tmc::asio_safe_timer::timer_type{tmc::asio_executor()};
}

static safe_socket::socket_type make_socket() {
  return safe_socket::socket_type{tmc::asio_executor()};
}

// Opens, binds, and listens on an ephemeral localhost port.
static tmc::task<tcp::endpoint> listen_local(safe_acceptor& Acc) {
  auto ec = co_await Acc.open(tcp::v4());
  EXPECT_FALSE(ec);
  ec = co_await Acc.set_option(asio_ns::socket_base::reuse_address{true});
  EXPECT_FALSE(ec);
  ec = co_await Acc.bind({asio_ns::ip::make_address("127.0.0.1"), 0});
  EXPECT_FALSE(ec);
  ec = co_await Acc.listen();
  EXPECT_FALSE(ec);
  co_return Acc.acceptor_unsafe().local_endpoint();
}

TEST_F(CATEGORY, timer_wait) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::asio_safe_timer timer{make_timer()};
    auto start = std::chrono::steady_clock::now();
    auto [ec] = co_await timer.async_wait_for(std::chrono::milliseconds(20));
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_FALSE(ec);
    EXPECT_GE(elapsed, std::chrono::milliseconds(19));
  }());
}

TEST_F(CATEGORY, timer_wait_until) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::asio_safe_timer timer{make_timer()};
    auto expiry = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
    auto [ec] = co_await timer.async_wait_until(expiry);
    EXPECT_FALSE(ec);
    EXPECT_GE(std::chrono::steady_clock::now(), expiry);
  }());
}

static tmc::task<int> timed_wait_report_abort(tmc::asio_safe_timer& T) {
  auto [ec] = co_await T.async_wait_for(std::chrono::milliseconds(50));
  co_return ec == asio_ns::error::operation_aborted ? 1 : 0;
}

// Two coroutines share one timer: the second async_wait_for re-arms the
// expiry, aborting the wait that was initiated first.
TEST_F(CATEGORY, timer_rearm_aborts_prior_wait) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::asio_safe_timer timer{make_timer()};
    auto [a, b] = co_await tmc::spawn_tuple(
      timed_wait_report_abort(timer), timed_wait_report_abort(timer)
    );
    // Normally exactly 1. May be 0 if the first wait somehow expired before
    // the second was initiated (extremely slow machine); never 2.
    EXPECT_LE(a + b, 1);
  }());
}

static tmc::task<int> long_wait_report_abort(tmc::asio_safe_timer& T) {
  auto [ec] = co_await T.async_wait_for(std::chrono::seconds(60));
  co_return ec == asio_ns::error::operation_aborted ? 1 : 0;
}

TEST_F(CATEGORY, timer_cancel_count) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::asio_safe_timer timer{make_timer()};
    auto waiter = tmc::spawn(long_wait_report_abort(timer)).fork();
    tmc::asio_safe_timer delay{make_timer()};
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

static tmc::task<void> echo_server(safe_acceptor& Acc) {
  auto [ec, sock] = co_await Acc.async_accept();
  EXPECT_FALSE(ec);
  safe_socket safe{std::move(sock)};
  char buf[5]{};
  auto [rec, rn] = co_await safe.async_read(asio_ns::buffer(buf, 5));
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, 5u);
  auto [wec, wn] = co_await safe.async_write(asio_ns::buffer(buf, 5));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);
  auto sec = co_await safe.close();
  EXPECT_FALSE(sec);
}

static tmc::task<void> echo_client(tcp::endpoint Ep) {
  safe_socket safe{make_socket()};
  auto [cec] = co_await safe.async_connect(Ep);
  EXPECT_FALSE(cec);
  char out[5] = {'h', 'e', 'l', 'l', 'o'};
  auto [wec, wn] = co_await safe.async_write(asio_ns::buffer(out, 5));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);
  char in[5]{};
  auto [rec, rn] = co_await safe.async_read(asio_ns::buffer(in, 5));
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, 5u);
  EXPECT_EQ(0, std::memcmp(out, in, 5));
  auto sec = co_await safe.close();
  EXPECT_FALSE(sec);
}

TEST_F(CATEGORY, socket_echo) {
  test_async_main(ex(), []() -> tmc::task<void> {
    safe_acceptor acc{safe_acceptor::acceptor_type{tmc::asio_executor()}};
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

static tmc::task<void> big_server(safe_acceptor& Acc) {
  auto [ec, sock] = co_await Acc.async_accept();
  EXPECT_FALSE(ec);
  safe_socket safe{std::move(sock)};
  std::vector<char> data(BIG_SIZE);
  // Read into a multi-buffer sequence to exercise buffer iteration.
  std::array<asio_ns::mutable_buffer, 2> bufs{
    asio_ns::buffer(data.data(), BIG_SIZE / 2),
    asio_ns::buffer(data.data() + BIG_SIZE / 2, BIG_SIZE - BIG_SIZE / 2)
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
  auto [wec, wn] = co_await safe.async_write(asio_ns::buffer(&ack, 1));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 1u);
  auto sec = co_await safe.close();
  EXPECT_FALSE(sec);
}

static tmc::task<void> big_client(tcp::endpoint Ep) {
  safe_socket safe{make_socket()};
  auto [cec] = co_await safe.async_connect(Ep);
  EXPECT_FALSE(cec);
  std::vector<char> data(BIG_SIZE);
  for (std::size_t i = 0; i < BIG_SIZE; ++i) {
    data[i] = big_pattern(i);
  }
  auto [wec, wn] = co_await safe.async_write(asio_ns::buffer(data));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, BIG_SIZE);
  char ack{};
  auto [rec, rn] = co_await safe.async_read(asio_ns::buffer(&ack, 1));
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, 1u);
  EXPECT_EQ(ack, 'A');
  auto sec = co_await safe.close();
  EXPECT_FALSE(sec);
}

TEST_F(CATEGORY, socket_large_transfer_multibuffer) {
  test_async_main(ex(), []() -> tmc::task<void> {
    safe_acceptor acc{safe_acceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    co_await tmc::spawn_tuple(big_server(acc), big_client(ep));
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

static tmc::task<void> eof_server(safe_acceptor& Acc) {
  auto [ec, sock] = co_await Acc.async_accept();
  EXPECT_FALSE(ec);
  safe_socket safe{std::move(sock)};
  char buf[10]{};
  // The peer sends only 5 bytes and then closes; expect a partial read
  // terminated by EOF, like asio::async_read.
  auto [rec, rn] = co_await safe.async_read(asio_ns::buffer(buf));
  EXPECT_EQ(rec, asio_ns::error::eof);
  EXPECT_EQ(rn, 5u);
  auto cec = co_await safe.close();
  EXPECT_FALSE(cec);
}

static tmc::task<void> eof_client(tcp::endpoint Ep) {
  safe_socket safe{make_socket()};
  auto [cec] = co_await safe.async_connect(Ep);
  EXPECT_FALSE(cec);
  char out[5] = {'a', 'b', 'c', 'd', 'e'};
  auto [wec, wn] = co_await safe.async_write(asio_ns::buffer(out, 5));
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);
  auto sec = co_await safe.shutdown(tcp::socket::shutdown_send); // sends FIN
  EXPECT_FALSE(sec);
}

TEST_F(CATEGORY, socket_partial_read_eof) {
  test_async_main(ex(), []() -> tmc::task<void> {
    safe_acceptor acc{safe_acceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    co_await tmc::spawn_tuple(eof_server(acc), eof_client(ep));
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

static tmc::task<void> pending_reader(safe_socket& S) {
  char buf[64]{};
  auto [ec, n] = co_await S.async_read(asio_ns::buffer(buf));
  EXPECT_TRUE(static_cast<bool>(ec));
  EXPECT_EQ(n, 0u);
}

static tmc::task<void> concurrent_closer(safe_socket& S) {
  tmc::asio_safe_timer t{make_timer()};
  co_await t.async_wait_for(std::chrono::milliseconds(1));
  auto ec = co_await S.close();
  EXPECT_FALSE(ec);
}

// close() from one coroutine while another coroutine's async_read is
// pending on the same socket. All initiations are serialized by the internal
// mutex, so the read must fail cleanly at a chunk boundary.
TEST_F(CATEGORY, socket_shutdown_during_read) {
  test_async_main(ex(), []() -> tmc::task<void> {
    safe_acceptor acc{safe_acceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    for (int i = 0; i < 10; ++i) {
      safe_socket client{make_socket()};
      auto [cec] = co_await client.async_connect(ep);
      EXPECT_FALSE(cec);
      auto [aec, ssock] = co_await acc.async_accept();
      EXPECT_FALSE(aec);
      safe_socket server{std::move(ssock)};
      co_await tmc::spawn_tuple(pending_reader(server), concurrent_closer(server));
      auto clec = co_await client.close();
      EXPECT_FALSE(clec);
    }
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

static tmc::task<void> accept_expect_abort(safe_acceptor& Acc, std::atomic<bool>& Done) {
  auto [aec, sock] = co_await Acc.async_accept();
  Done.store(true, std::memory_order_relaxed);
  EXPECT_EQ(aec, asio_ns::error::operation_aborted);
  (void)sock;
}

static tmc::task<void> cancel_until_done(safe_acceptor& Acc, std::atomic<bool>& Done) {
  tmc::asio_safe_timer delay{make_timer()};
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
    safe_acceptor acc{safe_acceptor::acceptor_type{tmc::asio_executor()}};
    co_await listen_local(acc);
    std::atomic<bool> done{false};
    co_await tmc::spawn_tuple(
      accept_expect_abort(acc, done), cancel_until_done(acc, done)
    );
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

static tmc::task<void> read_expect_abort(safe_socket& S, std::atomic<bool>& Done) {
  char buf[64]{};
  // The peer never sends these bytes, so this read only completes via cancel().
  auto [ec, n] = co_await S.async_read(asio_ns::buffer(buf));
  Done.store(true, std::memory_order_relaxed);
  EXPECT_EQ(ec, asio_ns::error::operation_aborted);
  EXPECT_EQ(n, 0u);
}

static tmc::task<void> cancel_read_until_done(safe_socket& S, std::atomic<bool>& Done) {
  tmc::asio_safe_timer delay{make_timer()};
  // Retry until the read has been initiated and aborted. cancel() must abort it
  // whether it lands on the pending read_some or between chunks.
  while (!Done.load(std::memory_order_relaxed)) {
    auto ec = co_await S.cancel();
    EXPECT_FALSE(ec);
    co_await delay.async_wait_for(std::chrono::milliseconds(1));
  }
}

// cancel() aborts an in-progress async_read() that is waiting for data the peer
// never sends.
TEST_F(CATEGORY, socket_cancel_pending_read) {
  test_async_main(ex(), []() -> tmc::task<void> {
    safe_acceptor acc{safe_acceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);
    safe_socket client{make_socket()};
    auto [cec] = co_await client.async_connect(ep);
    EXPECT_FALSE(cec);
    auto [aec, ssock] = co_await acc.async_accept();
    EXPECT_FALSE(aec);
    safe_socket server{std::move(ssock)};
    std::atomic<bool> done{false};
    co_await tmc::spawn_tuple(
      read_expect_abort(server, done), cancel_read_until_done(server, done)
    );
    auto clec = co_await client.close();
    EXPECT_FALSE(clec);
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

// ---- Accessor / option / send-receive coverage for the new forwarded methods.

TEST_F(CATEGORY, timer_accessors) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::asio_safe_timer timer{make_timer()};
    auto before = std::chrono::steady_clock::now();
    // expires_after sets the expiry without initiating a wait; nothing to cancel.
    std::size_t cancelled = co_await timer.expires_after(std::chrono::seconds(60));
    EXPECT_EQ(cancelled, 0u);
    auto exp = co_await timer.expiry();
    EXPECT_GE(exp, before + std::chrono::seconds(59));
    // No outstanding waits, so cancel_one cancels nothing.
    std::size_t one = co_await timer.cancel_one();
    EXPECT_EQ(one, 0u);
    // expires_at sets an absolute expiry that reads back exactly.
    auto target = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    co_await timer.expires_at(target);
    EXPECT_EQ(co_await timer.expiry(), target);
    (void)co_await timer.get_executor();
  }());
}

static tmc::task<void> accessors_server(safe_acceptor& Acc) {
  auto [aec, sock, peer] = co_await Acc.async_accept_endpoint();
  EXPECT_FALSE(aec);
  EXPECT_TRUE(peer.address().is_loopback());
  safe_socket safe{std::move(sock)};

  // no_delay round-trips through set_option / get_option.
  auto sec = co_await safe.set_option(tcp::no_delay{true});
  EXPECT_FALSE(sec);
  auto [gec, nd] = co_await safe.get_option(tcp::no_delay{});
  EXPECT_FALSE(gec);
  EXPECT_TRUE(nd.value());

  // Receive with flags, echo back with flags.
  char buf[5]{};
  auto [rec, rn] = co_await safe.async_receive(asio_ns::buffer(buf), 0);
  EXPECT_FALSE(rec);
  EXPECT_EQ(rn, 5u);
  auto [wec, wn] = co_await safe.async_send(asio_ns::buffer(buf, rn), 0);
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);

  auto cec = co_await safe.close();
  EXPECT_FALSE(cec);
}

static tmc::task<void> accessors_client(tcp::endpoint Ep) {
  safe_socket safe{make_socket()};
  auto [cec] = co_await safe.async_connect(Ep);
  EXPECT_FALSE(cec);

  // Endpoint accessors.
  auto [lec, lep] = co_await safe.local_endpoint();
  EXPECT_FALSE(lec);
  EXPECT_TRUE(lep.address().is_loopback());
  auto [rec, rep] = co_await safe.remote_endpoint();
  EXPECT_FALSE(rec);
  EXPECT_EQ(rep, Ep);

  // non_blocking getter/setter round-trip.
  EXPECT_FALSE(co_await safe.non_blocking(true));
  EXPECT_TRUE(co_await safe.non_blocking());
  EXPECT_FALSE(co_await safe.non_blocking(false));

  // Instantiate/smoke-test the remaining accessors (results are platform- or
  // state-dependent, so only the error codes are checked where meaningful).
  (void)co_await safe.native_non_blocking();
  (void)co_await safe.get_executor();
  (void)co_await safe.native_handle();
  asio_ns::socket_base::bytes_readable cmd;
  auto [icec, ic] = co_await safe.io_control(cmd);
  EXPECT_FALSE(icec);
  (void)ic.get();
  auto [avec, avail] = co_await safe.available();
  EXPECT_FALSE(avec);
  (void)avail;
  auto [amec, marked] = co_await safe.at_mark();
  EXPECT_FALSE(amec);
  (void)marked;

  // A connected socket is immediately ready to write.
  auto [wwec] = co_await safe.async_wait(asio_ns::socket_base::wait_write);
  EXPECT_FALSE(wwec);

  // Send with flags, receive the echo without flags.
  char out[5] = {'h', 'e', 'l', 'l', 'o'};
  auto [wec, wn] = co_await safe.async_send(asio_ns::buffer(out), 0);
  EXPECT_FALSE(wec);
  EXPECT_EQ(wn, 5u);
  char in[5]{};
  auto [rrec, rrn] = co_await safe.async_receive(asio_ns::buffer(in));
  EXPECT_FALSE(rrec);
  EXPECT_EQ(rrn, 5u);
  EXPECT_EQ(0, std::memcmp(out, in, 5));

  auto scec = co_await safe.close();
  EXPECT_FALSE(scec);
}

TEST_F(CATEGORY, socket_accessors_and_send_receive) {
  test_async_main(ex(), []() -> tmc::task<void> {
    safe_acceptor acc{safe_acceptor::acceptor_type{tmc::asio_executor()}};
    auto ep = co_await listen_local(acc);

    // Acceptor accessors.
    auto [lec, lep] = co_await acc.local_endpoint();
    EXPECT_FALSE(lec);
    EXPECT_EQ(lep, ep);
    auto [oec, ra] = co_await acc.get_option(asio_ns::socket_base::reuse_address{});
    EXPECT_FALSE(oec);
    (void)ra.value();
    (void)co_await acc.get_executor();
    (void)co_await acc.native_handle();
    (void)co_await acc.non_blocking();
    (void)co_await acc.native_non_blocking();
    asio_ns::socket_base::bytes_readable acmd;
    (void)co_await acc.io_control(acmd);

    co_await tmc::spawn_tuple(accessors_server(acc), accessors_client(ep));
    auto ec = co_await acc.close();
    EXPECT_FALSE(ec);
  }());
}

#undef CATEGORY

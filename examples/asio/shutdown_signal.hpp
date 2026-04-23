#pragma once

#include "asio/signal_set.hpp"
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/manual_reset_event.hpp"
#include "tmc/sync.hpp"

#include <optional>

namespace detail {
inline std::optional<asio::signal_set> g_shutdown_signal;
}

inline tmc::manual_reset_event g_shutdown_event;

// Call install_signal_handler() on program startup. This will handle shutdown
// signals sent by the OS. Then any number of coroutines can `co_await
// g_shutdown_event` or poll it with `g_shutdown_event.is_set()`.

inline void install_signal_handler(tmc::ex_asio& ex) {
  detail::g_shutdown_signal.emplace(ex.ioc, SIGINT, SIGTERM);
  tmc::post(ex, []() -> tmc::task<void> {
    co_await detail::g_shutdown_signal->async_wait(tmc::aw_asio);
    g_shutdown_event.set();
  }());
}

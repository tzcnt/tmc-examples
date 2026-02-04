#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cstdio>
#include <optional>

// Option 2: Using a channel, but shrunk to the minimum size.
// This only allows a single waiter.

struct OneShotChanConfig : tmc::chan_default_config {
  static inline constexpr size_t BlockSize = 1;
  static inline constexpr bool EmbedFirstBlock = true;
};

template <typename T>
using OneShotChanTok = tmc::chan_tok<T, OneShotChanConfig>;

static tmc::task<void> OneShotProducer(OneShotChanTok<int> chan, bool ok) {
  if (ok) {
    chan.post(5);
  }
  co_await chan.drain();
}

static tmc::task<void> OneShotConsumer(OneShotChanTok<int> chan) {
  if (auto v = co_await chan.pull()) {
    std::printf("Got value: %d\n", *v);
  } else {
    std::printf("Got nothing\n");
  }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  tmc::async_main([]() -> tmc::task<int> {
    {
      // produces a value
      auto chan = tmc::make_channel<int, OneShotChanConfig>();
      co_await tmc::spawn_tuple(
        OneShotProducer(chan, true), OneShotConsumer(chan)
      );
    }
    {
      // produces nothing
      auto chan = tmc::make_channel<int, OneShotChanConfig>();
      co_await tmc::spawn_tuple(
        OneShotProducer(chan, false), OneShotConsumer(chan)
      );
    }
    co_return 0;
  }());
}

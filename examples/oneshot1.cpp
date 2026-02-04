#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <cstdio>
#include <memory>
#include <optional>

// Option 1: Make your own promise/future.
// Note - this has extra boilerplate to ensure shared ownership semantics. This
// may not be necessary for your use case.

template <typename T> struct OneShotStruct {
  std::optional<T> value;
  tmc::manual_reset_event event;
  OneShotStruct() : value{}, event{} {}
};

template <typename T> class OneShotPromise;

template <typename T> class OneShotFuture {
  std::shared_ptr<OneShotStruct<T>> ptr;

  friend class OneShotPromise<T>;

  OneShotFuture(std::shared_ptr<OneShotStruct<T>> Ptr) : ptr{Ptr} {}

public:
  tmc::task<bool> await() {
    co_await ptr->event;
    co_return ptr->value.has_value();
  }
  T& value() { return *ptr->value; }
};

template <typename T> class OneShotPromise {
  std::shared_ptr<OneShotStruct<T>> ptr;

public:
  OneShotPromise() { ptr = std::make_shared<OneShotStruct<T>>(); }
  OneShotPromise(OneShotPromise&& rhs) : ptr{std::move(rhs.ptr)} {}
  OneShotPromise& operator=(OneShotPromise&& rhs) { ptr = std::move(rhs.ptr); }
  OneShotPromise(OneShotPromise const&) = delete;
  OneShotPromise& operator=(OneShotPromise const&) = delete;

  OneShotFuture<T> get_future() { return OneShotFuture<T>{ptr}; }

  void set_value(T&& t) {
    ptr->value.emplace(std::move(t));
    ptr->event.set();
  }
  ~OneShotPromise() { ptr->event.set(); }
};

static tmc::task<void> OneShotProducer(OneShotPromise<int> prom, bool ok) {
  if (ok) {
    prom.set_value(5);
  }
  co_return;
}

static tmc::task<void> OneShotConsumer(OneShotFuture<int> fut) {
  if (co_await fut.await()) {
    std::printf("Got value: %d\n", fut.value());
  } else {
    std::printf("Got nothing\n");
  }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  tmc::async_main([]() -> tmc::task<int> {
    {
      // produces a value
      auto prom = OneShotPromise<int>{};
      auto fut = prom.get_future();
      co_await tmc::spawn_tuple(
        OneShotProducer(std::move(prom), true), OneShotConsumer(fut)
      );
    }
    {
      // produces nothing
      auto prom = OneShotPromise<int>{};
      auto fut = prom.get_future();
      co_await tmc::spawn_tuple(
        OneShotProducer(std::move(prom), false), OneShotConsumer(fut)
      );
    }
    co_return 0;
  }());
}

// A simple example that shows a tmc::task awaiting an external awaitable.
// Since this external awaitable has no knowledge of TMC, it resumes the task on
// its own thread. This is not an ideal way to integrate with TMC, but it proves
// that it can work.

// For the "proper" way to implement an external awaitable that resumes the
// task resume back on its original TMC executor, see the implementation of
// tmc::aw_asio_base::callback::operator() in
// https://github.com/tzcnt/tmc-asio/blob/main/include/tmc/asio/aw_asio.hpp

#include <coroutine>
#include <iostream>
#include <thread>
#include <utility>

#define TMC_IMPL
#include "external_executor.hpp"
#include "tmc/all_headers.hpp"

template <typename Result> class external_awaitable {
  Result result;
  std::coroutine_handle<> outer;

public:
  external_awaitable() = default;
  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> Outer) noexcept {
    outer = Outer;
    external_executor().post([this]() {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      result = 42;
      outer.resume();
    });
  }

  Result await_resume() & noexcept { return result; }
  Result await_resume() && noexcept { return std::move(result); }
};

tmc::task<int> coro(external_awaitable<int>& Awaitable) {
  std::cout << "started on " << this_thread_id() << std::endl;
  std::cout << "co_awaiting..." << std::endl;
  auto result = co_await Awaitable;
  std::cout << "resumed on " << this_thread_id() << std::endl;
  if (result != 42) {
    std::printf("wrong result from external_awaitable\n");
  }
  co_return result;
}

int main() {
  tmc::cpu_executor().init();
  external_awaitable<int> awaitable{};
  std::future<int> result_future =
    tmc::post_waitable(tmc::cpu_executor(), coro(awaitable), 0);
  int result = result_future.get();
  if (result != 42) {
    std::printf("wrong result from result_future\n");
  }
}

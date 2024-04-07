// A simple example that shows a tmc::task awaiting an external awaitable.
// Since this external awaitable has no knowledge of TMC, it resumes the task on
// its own thread. If you wanted to move the task back to the original TMC
// executor, you would have to do so manually using tmc::resume_on.

// For the "proper" way to implement an external awaitable that resumes the
// task resume back on its original TMC executor, see the implementation of
// tmc::aw_asio_base::callback::operator() in
// https://github.com/tzcnt/tmc-asio/blob/main/include/tmc/asio/aw_asio.hpp

#include <coroutine>
#include <thread>
#include <utility>

#define TMC_IMPL
#include "tmc/all_headers.hpp"

#include "util/thread_name.hpp"

template <typename Result> class external_awaitable {
  Result result;
  std::coroutine_handle<> outer;

public:
  external_awaitable() = default;
  bool await_ready() { return false; }

  void await_suspend(std::coroutine_handle<> Outer) noexcept {
    outer = Outer;

    std::thread([this]() {
      std::this_thread::sleep_for(std::chrono::seconds(1));
      result = 42;
      outer.resume();
    }).detach();
  }

  Result await_resume() & noexcept { return result; }
  Result await_resume() && noexcept { return std::move(result); }
};

tmc::task<int> coro() {
  std::printf("started on %s\n", get_thread_name().c_str());
  std::printf("co_awaiting...\n");
  auto result = co_await external_awaitable<int>{};
  std::printf("resumed on %s\n", get_thread_name().c_str());
  if (result != 42) {
    std::printf("wrong result from external_awaitable\n");
  }
  co_return result;
}

int main() {
  hook_init_ex_cpu_thread_name(tmc::cpu_executor());
  tmc::cpu_executor().init();
  std::future<int> result_future =
    tmc::post_waitable(tmc::cpu_executor(), coro(), 0);
  int result = result_future.get();
  if (result != 42) {
    std::printf("wrong result from result_future\n");
  }
}

#include <coroutine>
#include <iostream>
#include <thread>

#define TMC_IMPL
#include "tmc/all_headers.hpp"

// A simple "external" awaitable coroutine type that has no knowledge of TMC.
template <typename Result> class aw_external_coro;
template <typename Result> struct external_coro_promise;

template <typename Result>
struct external_coro : std::coroutine_handle<external_coro_promise<Result>> {
  using result_type = Result;
  using promise_type = external_coro_promise<Result>;
  aw_external_coro<Result> operator co_await() {
    return aw_external_coro<Result>(*this);
  }
};

template <typename Result> class aw_external_coro_final_suspend {
  std::coroutine_handle<> handle;
  friend struct external_coro_promise<Result>;
  aw_external_coro_final_suspend(const std::coroutine_handle<>& Handle)
      : handle(Handle) {}

public:
  bool await_ready() const noexcept { return false; }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> ending
  ) const noexcept {
    return handle;
  }
  constexpr void await_resume() const noexcept {}
};

template <typename Result> struct external_coro_promise {
  external_coro_promise() {}
  constexpr std::suspend_always initial_suspend() const noexcept { return {}; }
  constexpr aw_external_coro_final_suspend<Result>
  final_suspend() const noexcept {
    return {continuation};
  }
  external_coro<Result> get_return_object() noexcept {
    return {external_coro<Result>::from_promise(*this)};
  }
  void unhandled_exception() { throw; }
  void return_value(Result&& Value) { *result_ptr = std::move(Value); }
  void return_value(const Result& Value) { *result_ptr = Value; }
  std::coroutine_handle<> continuation;
  Result* result_ptr;
};

template <> struct external_coro_promise<void> {
  external_coro_promise() {}
  constexpr std::suspend_always initial_suspend() const noexcept { return {}; }
  constexpr std::suspend_never final_suspend() const noexcept { return {}; }
  external_coro<void> get_return_object() noexcept {
    return {external_coro<void>::from_promise(*this)};
  }
  void unhandled_exception() { throw; }
  void return_void() {}
  std::coroutine_handle<> continuation;
};

template <typename Result> class aw_external_coro {
  external_coro<Result> handle;
  Result result;

  friend struct external_coro<Result>;
  constexpr aw_external_coro(const external_coro<Result>& Handle)
      : handle(Handle) {}

public:
  constexpr bool await_ready() const noexcept { return handle.done(); }
  constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<> Outer
  ) noexcept {
    auto& p = handle.promise();
    p.continuation = Outer;
    p.result_ptr = &result;
    return handle;
  }
  constexpr Result& await_resume() & noexcept { return result; }
  constexpr Result&& await_resume() && noexcept { return std::move(result); }
};

template <> class aw_external_coro<void> {
  external_coro<void> handle;
  friend struct external_coro<void>;
  constexpr aw_external_coro(const external_coro<void>& Handle)
      : handle(Handle) {}

public:
  bool await_ready() const noexcept { return handle.done(); }
  std::coroutine_handle<> await_suspend(std::coroutine_handle<> Outer
  ) const noexcept {
    auto& p = handle.promise();
    p.continuation = Outer;
    return handle;
  }
  constexpr void await_resume() const noexcept {}
};

external_coro<int> external(int Count, int Max);

tmc::task<int> internal(int Count, int Max) {
  ++Count;
  if (Count == Max) {
    co_return Count;
  }
  std::printf("internal pre %d\n", Count);
  int result = co_await external(Count, Max);
  std::printf("internal post %d\n", Count);
  co_return result;
}

external_coro<int> external(int Count, int Max) {
  ++Count;
  if (Count == Max) {
    co_return Count;
  }
  std::printf("external pre %d\n", Count);
  int result = co_await internal(Count, Max);
  std::printf("external post %d\n", Count);
  co_return result;
}

void run_internal_first(int max) {
  std::future<int> result_future =
    tmc::post_waitable(tmc::cpu_executor(), internal(0, max), 0);
  int result = result_future.get();
  if (result != max) {
    std::printf("wrong result from result_future\n");
  }
}

// void run_external_first(int max) {
//   std::future<int> result_future =
//     tmc::post_waitable(tmc::cpu_executor(), external(0, max), 0);
//   int result = result_future.get();
//   if (result != max) {
//     std::printf("wrong result from result_future\n");
//   }
// }

int main() {
  tmc::cpu_executor().init();
  run_internal_first(4);
  run_internal_first(5);
}

#pragma once

#include <coroutine>
#include <exception>
#include <utility>

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
  std::coroutine_handle<> await_suspend(
    [[maybe_unused]] std::coroutine_handle<> ending
  ) const noexcept {
    return handle;
  }
  inline void await_resume() const noexcept {}
};

template <typename Result> struct external_coro_promise {
  std::coroutine_handle<> continuation;
  Result* result_ptr = nullptr;

  external_coro_promise() {}
  inline std::suspend_always initial_suspend() const noexcept { return {}; }
  inline aw_external_coro_final_suspend<Result> final_suspend() const noexcept {
    return {continuation};
  }
  external_coro<Result> get_return_object() noexcept {
    return {external_coro<Result>::from_promise(*this)};
  }
  [[noreturn]] void unhandled_exception() { std::terminate(); }
  void return_value(Result&& Value) { *result_ptr = std::move(Value); }
  void return_value(const Result& Value) { *result_ptr = Value; }
};

template <> struct external_coro_promise<void> {
  std::coroutine_handle<> continuation;

  external_coro_promise() {}
  inline std::suspend_always initial_suspend() const noexcept { return {}; }
  inline std::suspend_never final_suspend() const noexcept { return {}; }
  external_coro<void> get_return_object() noexcept {
    return {external_coro<void>::from_promise(*this)};
  }
  [[noreturn]] void unhandled_exception() { std::terminate(); }
  void return_void() {}
};

template <typename Result> class aw_external_coro {
  external_coro<Result> handle;
  Result result;

  friend struct external_coro<Result>;
  inline aw_external_coro(const external_coro<Result>& Handle)
      : handle(Handle) {}

public:
  inline bool await_ready() const noexcept { return handle.done(); }
  inline std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> Outer) noexcept {
    auto& p = handle.promise();
    p.continuation = Outer;
    p.result_ptr = &result;
    return handle;
  }
  inline Result& await_resume() & noexcept { return result; }
  inline Result&& await_resume() && noexcept { return std::move(result); }
};

template <> class aw_external_coro<void> {
  external_coro<void> handle;
  friend struct external_coro<void>;
  inline aw_external_coro(const external_coro<void>& Handle) : handle(Handle) {}

public:
  bool await_ready() const noexcept { return handle.done(); }
  std::coroutine_handle<>
  await_suspend(std::coroutine_handle<> Outer) const noexcept {
    auto& p = handle.promise();
    p.continuation = Outer;
    return handle;
  }
  inline void await_resume() const noexcept {}
};

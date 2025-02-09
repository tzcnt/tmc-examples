// Turn an a callback-style async operation into a TMC awaitable.
// The function callback_awaitable() expects the async operation initiation
// function and the set of initiation arguments.
// See the usage examples in callback_awaitable.cpp.

// This operates on a particular kind of async operation which accepts a
// `user_data` pointer and provides it back to the callback on completion.
// It uses this pointer to store the awaitable information.

// The async operation initiation function should be of the form:
// void (*)(void* user_data, CallbackFunc*, InitArgs...)

// Its CallbackFunc parameter should be of the form:
// void (*)(void* user_data, ResultArgs...)

// The result of the awaitable will be a std::tuple<ResultArgs...>.

// This template isn't part of the core TMC library because it depends on the
// parameter order of the initiation / callback functions. If you have a similar
// use case, but the function parameters are reordered, you can repurpose this
// by changing the signature of `initiate_async_fn` and `callback`.

#pragma once

#include "tmc/detail/compat.hpp"   // for TMC_FORCE_INLINE
#include "tmc/detail/concepts.hpp" // for result_storage_t, awaitable_traits
#include "tmc/detail/thread_locals.hpp" // for this_thread
#include "tmc/task.hpp"

#include <coroutine>
#include <tuple>
#include <utility>

namespace tmc::detail {
struct AwCallbackTag {};
} // namespace tmc::detail

template <typename Awaitable> struct callback_awaitable_impl {
  // Keep an lvalue reference to handle. Depends on temporary lifetime extension
  // when used in some contexts. Safe as long as you don't manually call
  // operator co_await() on a temporary and try to save this for later.
  Awaitable& handle;
  tmc::detail::result_storage_t<typename Awaitable::ResultTuple> result;

  friend Awaitable;
  callback_awaitable_impl(Awaitable& Handle) : handle(Handle) {}

  bool await_ready() { return false; }

  TMC_FORCE_INLINE inline void await_suspend(std::coroutine_handle<> Outer
  ) noexcept {
    handle.customizer.continuation = Outer.address();
    handle.customizer.result_ptr = &result;
    handle.async_initiate();
  }

  auto await_resume() noexcept {
    // Move the result out of the optional
    // (returns tuple<Result>, not optional<tuple<Result>>)
    if constexpr (std::is_default_constructible_v<
                    typename Awaitable::ResultTuple>) {
      return std::move(result);
    } else {
      return *std::move(result);
    }
  }
};

template <typename... ResultArgs> struct callback_awaitable_base {
  using ResultTuple = std::tuple<ResultArgs...>;
  tmc::detail::awaitable_customizer<ResultTuple> customizer;
  size_t prio;

  callback_awaitable_base() : prio(tmc::detail::this_thread::this_task.prio) {}

  template <typename... ResultArgs_>
  static inline void callback(void* user_data, ResultArgs_... results) {
    auto aw = static_cast<callback_awaitable_base*>(user_data);
    if constexpr (std::is_default_constructible_v<std::tuple<ResultArgs...>>) {
      *aw->customizer.result_ptr =
        std::tuple<ResultArgs...>(std::forward<ResultArgs_>(results)...);
    } else {
      aw->customizer.result_ptr->emplace(std::forward<ResultArgs_>(results)...);
    }

    auto next = aw->customizer.resume_continuation(aw->prio);
    if (next != std::noop_coroutine()) {
      next.resume();
    }
  }

  virtual void async_initiate() = 0;

public:
  callback_awaitable_base(const callback_awaitable_base&) = default;
  callback_awaitable_base(callback_awaitable_base&&) = default;
  callback_awaitable_base& operator=(const callback_awaitable_base&) = default;
  callback_awaitable_base& operator=(callback_awaitable_base&&) = default;
  virtual ~callback_awaitable_base() = default;
};

template <typename... ResultArgs> struct wrapper {
  template <typename Init, typename... InitArgs>
  struct [[nodiscard]] callback_awaitable final
      : public callback_awaitable_base<ResultArgs...>,
        tmc::detail::AwCallbackTag {
    Init initiate_async_fn;
    std::tuple<InitArgs...> init_args;

    template <typename Init_, typename... InitArgs_>
    callback_awaitable(Init_&& Initiation, InitArgs_&&... Args)
        : initiate_async_fn(std::forward<Init_>(Initiation)),
          init_args(std::forward<InitArgs_>(Args)...) {}

    void async_initiate() final override {
      std::apply(
        [&](InitArgs&&... Args) {
          std::move(initiate_async_fn)(
            static_cast<callback_awaitable_base<ResultArgs...>*>(this),
            &callback_awaitable_base<ResultArgs...>::callback,
            std::move(Args)...
          );
        },
        std::move(init_args)
      );
    }

    callback_awaitable_impl<callback_awaitable> operator co_await() {
      return callback_awaitable_impl<callback_awaitable>(*this);
    }
  };
};

template <typename S> struct func_params;

template <typename Result, typename... Args>
struct func_params<Result(Args...)> {
  using result_type = Result;
  using arg_tuple = typename std::tuple<Args...>;
};

// Constructor which produces an awaitable from external async function
// pointer/args.
template <typename CallbackFunc, typename... InitArgs>
auto await_callback(
  void (*async_func)(void*, CallbackFunc*, InitArgs...),
  InitArgs... async_init_args
) {
  return []<typename... ResultArgs>(
           std::type_identity<std::tuple<void*, ResultArgs...>>,
           void (*fn)(void*, CallbackFunc*, InitArgs...), InitArgs... args
         ) -> auto {
    typename wrapper<ResultArgs...>::template callback_awaitable<
      void (*)(void*, CallbackFunc*, InitArgs...), InitArgs...>
    aw(fn, std::forward<InitArgs>(args)...);
    return aw;
  }(std::type_identity<typename func_params<CallbackFunc>::arg_tuple>{},
           async_func, std::forward<InitArgs>(async_init_args)...);
}

// Implementation of tmc::detail::awaitable_traits which fully integrates this
// awaitable type with the library.
namespace tmc::detail {

template <typename T>
concept IsAwCallback = std::is_base_of_v<tmc::detail::AwCallbackTag, T>;

template <IsAwCallback Awaitable> struct awaitable_traits<Awaitable> {
  using result_type = typename Awaitable::ResultTuple;
  using self_type = Awaitable;

  // Values controlling the behavior when awaited directly in a tmc::task
  static decltype(auto) get_awaiter(self_type&& awaitable) {
    return std::forward<self_type>(awaitable).operator co_await();
  }

  // Values controlling the behavior when wrapped by a utility function
  // such as tmc::spawn_*()
  static constexpr configure_mode mode = ASYNC_INITIATE;
  static void async_initiate(
    self_type&& awaitable,
    [[maybe_unused]] tmc::detail::type_erased_executor* Executor,
    [[maybe_unused]] size_t Priority
  ) {
    awaitable.async_initiate();
  }

  static void set_result_ptr(
    self_type& awaitable, tmc::detail::result_storage_t<result_type>* ResultPtr
  ) {
    awaitable.customizer.result_ptr = ResultPtr;
  }

  static void set_continuation(self_type& awaitable, void* Continuation) {
    awaitable.customizer.continuation = Continuation;
  }

  static void set_continuation_executor(self_type& awaitable, void* ContExec) {
    awaitable.customizer.continuation_executor = ContExec;
  }

  static void set_done_count(self_type& awaitable, void* DoneCount) {
    awaitable.customizer.done_count = DoneCount;
  }

  static void set_flags(self_type& awaitable, size_t Flags) {
    awaitable.customizer.flags = Flags;
  }
};
} // namespace tmc::detail

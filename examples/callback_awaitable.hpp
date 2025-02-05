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

#include "tmc/detail/concepts.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/task.hpp"

#include <coroutine>
#include <tuple>
#include <utility>

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
      : public callback_awaitable_base<ResultArgs...> {
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
  using callback_signature = func_params<CallbackFunc>;
  return []<typename... ResultArgs>(
           std::type_identity<std::tuple<void*, ResultArgs...>>,
           void (*fn)(void*, CallbackFunc*, InitArgs...), InitArgs... args
         ) -> auto {
    typename wrapper<ResultArgs...>::template callback_awaitable<
      void (*)(void*, CallbackFunc*, InitArgs...), InitArgs...>
    aw(fn, std::forward<InitArgs>(args)...);
    return aw;
  }(std::type_identity<typename callback_signature::arg_tuple>{}, async_func,
           std::forward<InitArgs>(async_init_args)...);
}

// TODO implement tmc::detail::awaitable_traits

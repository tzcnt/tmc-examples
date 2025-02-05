// Example of transforming a callback-based async API into a TMC awaitable.
// Generic implementation is in callback_awaitable.hpp.

#define TMC_IMPL

#include "callback_awaitable.hpp"
#include "tmc/ex_cpu.hpp"

// Examples of callback-based async functions that might be provided by an
// external library.
void simulated_async_fn_void(void* user_data, void (*callback)(void*)) {
  callback(user_data);
}

void simulated_async_fn(
  void* user_data, void (*callback)(void*, float, float), int input
) {
  auto f = static_cast<float>(input);
  callback(user_data, f + 1.0f, f + 2.0f);
}

void simulated_async_fn2(
  void* user_data, void (*callback)(void*, int), float input1, float input2
) {
  auto result = static_cast<int>(input1 + input2);
  callback(user_data, result);
}

// Demonstrate returning a move-only type from the awaitable.
struct move_only_type {
  int value;

  move_only_type(int input) : value(input) {}
  move_only_type& operator=(move_only_type&&) = default;
  move_only_type(move_only_type&&) = default;
  ~move_only_type() = default;

  // No default or copy constructor
  move_only_type() = delete;
  move_only_type(const move_only_type&) = delete;
  move_only_type& operator=(const move_only_type&) = delete;
};

void simulated_async_fn_move_only(
  void* user_data, void (*callback)(void*, move_only_type), int input
) {
  callback(user_data, move_only_type{input + 1});
}

// Demonstrate usages of these functions with the callback_awaitable wrapper.
int main() {
  tmc::async_main([]() -> tmc::task<int> {
    co_await await_callback(simulated_async_fn_void);

    auto [result1, result2] = co_await await_callback(simulated_async_fn, 1);
    assert(result1 == 2.0f);
    assert(result2 == 3.0f);

    auto [result] = co_await await_callback(simulated_async_fn2, 4.0f, 6.0f);
    assert(result == 10);

    auto aw = await_callback(simulated_async_fn_move_only, 1);
    auto [move_only_result] = co_await aw;
    assert(move_only_result.value == 2);

    co_return 0;
  }());
}

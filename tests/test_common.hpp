#pragma once

#include "tmc/all_headers.hpp" // IWYU pragma: export
#include "tmc/utils.hpp"       // IWYU pragma: export

#include <climits>

#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define TSAN_ENABLED
#endif
#endif

static inline tmc::task<void> empty_task() { co_return; }

static inline tmc::task<int> int_task() { co_return 1; }

static inline tmc::task<void> capturing_task(std::atomic<int>& i) {
  ++i;
  i.notify_all();
  co_return;
}

static inline void empty_func() {}

static inline int int_func() { return 1; }

template <typename Arr> void inc(Arr& arr, size_t& idx) {
  arr[idx] = idx;
  ++idx;
};

template <typename Arr> tmc::task<void> inc_task(Arr& arr, size_t& idx) {
  arr[idx] = idx;
  ++idx;
  co_return;
};

template <typename Arr> tmc::task<size_t> inc_task_int(Arr& arr, size_t idx) {
  arr[idx] = idx;
  ++idx;
  co_return idx;
};

struct destructor_counter {
  std::atomic<size_t>* count;
  destructor_counter(std::atomic<size_t>* C) noexcept : count{C} {}
  destructor_counter(destructor_counter const& Other) = delete;
  destructor_counter& operator=(destructor_counter const& Other) = delete;

  destructor_counter(destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
  }
  destructor_counter& operator=(destructor_counter&& Other) noexcept {
    count = Other.count;
    Other.count = nullptr;
    return *this;
  }

  ~destructor_counter() {
    if (count != nullptr) {
      ++(*count);
    }
  }
};

// test_async_main_int is similar to tmc::async_main
// it returns an int value to be used as an "exit code"
template <typename Executor>
static inline tmc::task<void> test_async_main_int_task_(
  Executor& Exec, tmc::task<int> ClientMainTask, std::atomic<int>* ExitCode_out
) {
  int exitCode = co_await std::move(ClientMainTask.resume_on(Exec));
  ExitCode_out->store(exitCode);
  ExitCode_out->notify_all();
}

template <typename Executor>
static inline int
test_async_main_int(Executor& Exec, tmc::task<int>&& ClientMainTask) {
  // test setup should call init() beforehand
  std::atomic<int> exitCode(INT_MIN);
  post(
    Exec, test_async_main_int_task_(Exec, std::move(ClientMainTask), &exitCode),
    0
  );
  exitCode.wait(INT_MIN);
  return exitCode.load();
}

// test_async_main doesn't return a value
template <typename Executor>
static inline tmc::task<void> test_async_main_task_(
  Executor& Exec, tmc::task<void> ClientMainTask, std::atomic<int>* ExitCode_out
) {
  co_await std::move(ClientMainTask.resume_on(Exec));
  ExitCode_out->store(0);
  ExitCode_out->notify_all();
}

template <typename Executor>
static inline void
test_async_main(Executor& Exec, tmc::task<void>&& ClientMainTask) {
  // test setup should call init() beforehand
  std::atomic<int> exitCode(INT_MIN);
  post(
    Exec, test_async_main_task_(Exec, std::move(ClientMainTask), &exitCode), 0
  );
  exitCode.wait(INT_MIN);
}

template <typename Tuple> decltype(auto) sum_tuple(Tuple const& tuple) {
  return std::apply(
    [](auto const&... value) -> decltype(auto) { return (value + ...); }, tuple
  );
};

static inline bool unpredictable_filter(int i) { return i != 3; }

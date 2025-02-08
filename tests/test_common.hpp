#pragma once

#include "tmc/all_headers.hpp" // IWYU pragma: export
#include "tmc/utils.hpp"       // IWYU pragma: export

static inline tmc::task<void> empty_task() { co_return; }

static inline tmc::task<int> int_task() { co_return 1; }

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

template <typename Arr> tmc::task<int> inc_task_int(Arr& arr, size_t idx) {
  arr[idx] = idx;
  ++idx;
  co_return idx;
};

// test_async_main_int is similar to tmc::async_main
// it returns an int value to be used as an "exit code"
template <typename Executor>
static inline tmc::task<void> _test_async_main_int_task(
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
  std::atomic<int> exitCode(std::numeric_limits<int>::min());
  post(
    Exec, _test_async_main_int_task(Exec, std::move(ClientMainTask), &exitCode),
    0
  );
  exitCode.wait(std::numeric_limits<int>::min());
  return exitCode.load();
}

// test_async_main doesn't return a value
template <typename Executor>
static inline tmc::task<void> _test_async_main_task(
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
  std::atomic<int> exitCode(std::numeric_limits<int>::min());
  post(
    Exec, _test_async_main_task(Exec, std::move(ClientMainTask), &exitCode), 0
  );
  exitCode.wait(std::numeric_limits<int>::min());
}

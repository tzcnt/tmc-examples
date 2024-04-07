/// Generate human-readable thread names for various executors. Used by examples
/// that demonstrate switching tasks between executors.

#pragma once
#include "tmc/ex_cpu.hpp"
#include <sstream>
#include <string>

namespace this_thread {
// this constinit compiles now but it didn't before? is it fixed in gcc 13.2.1?
inline constinit std::string thread_name{};
} // namespace this_thread

/// This must be called before calling init() on the executor.
void hook_init_ex_cpu_thread_name(tmc::ex_cpu& Executor) {
  Executor.set_thread_init_hook([](size_t Slot) {
    this_thread::thread_name =
      std::string("cpu thread ") + std::to_string(Slot);
  });
}

// This has been observed to produce the wrong results (always prints the same
// thread name) on Clang 16, due to incorrectly caching thread_locals across
// suspend points. The issue has been resolved in Clang 17.
std::string get_thread_name() {
  std::string tmc_tid = this_thread::thread_name;
  if (!tmc_tid.empty()) {
    return tmc_tid;
  } else {
    static std::ostringstream id;
    id << std::this_thread::get_id();
    return "external thread " + id.str();
  }
}

void print_thread_name() { std::printf("%s\n", get_thread_name().c_str()); }
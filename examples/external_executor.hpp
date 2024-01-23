// A simple external executor that simply spawns a new thread for every
// submitted operation.

#pragma once
#include "tmc/detail/thread_locals.hpp"
#include <sstream>
#include <string>
#include <thread>

class external_executor {
public:
  template <typename Functor> void post(Functor func) {
    std::thread(func).detach();
  }
};

external_executor exec;
external_executor& external_executor() { return exec; }

std::string this_thread_id() {
  std::string tmc_tid = tmc::detail::this_thread::thread_name;
  if (!tmc_tid.empty()) {
    return tmc_tid;
  } else {
    static std::ostringstream id;
    id << std::this_thread::get_id();
    return "external thread " + id.str();
  }
}

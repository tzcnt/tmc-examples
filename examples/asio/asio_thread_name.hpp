#pragma once
#include "../util/thread_name.hpp"

#include "tmc/asio/ex_asio.hpp"

void hook_init_ex_asio_thread_name(tmc::ex_asio& Executor) {
  Executor.set_thread_init_hook([](size_t Slot) {
    this_thread::thread_name =
      std::string("i/o thread ") + std::to_string(Slot);
  });
}
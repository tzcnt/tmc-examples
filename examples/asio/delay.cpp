// Demonstrate the timer / delay capabilities of Asio executor
// Use resume_on() to send the task back and forth
// between asio_executor() and cpu_executor()
#ifdef _WIN32
#include <SDKDDKVer.h>
#endif

#define TMC_IMPL

#include "asio_thread_name.hpp"

#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/ex_cpu.hpp"

#include <asio/steady_timer.hpp>
#include <chrono>
#include <cstdio>

int main() {
  hook_init_ex_cpu_thread_name(tmc::cpu_executor());
  hook_init_ex_asio_thread_name(tmc::asio_executor());
  tmc::asio_executor().init();
  return tmc::async_main([]() -> tmc::task<int> {
    print_thread_name();
    for (size_t i = 0; i < 8; ++i) {
      asio::steady_timer tim{
        tmc::asio_executor(), std::chrono::milliseconds(250)
      };
      auto [error] =
        co_await tim.async_wait(tmc::aw_asio).resume_on(tmc::asio_executor());
      if (error) {
        std::printf("error: %s\n", error.message().c_str());
        co_return -1;
      }
      print_thread_name();

      auto [error2] =
        co_await asio::steady_timer{
          tmc::asio_executor(), std::chrono::milliseconds(250)
        }
          .async_wait(tmc::aw_asio)
          .resume_on(tmc::cpu_executor());
      if (error2) {
        std::printf("error2: %s\n", error.message().c_str());
        co_return -1;
      }
      print_thread_name();
    }
    co_return 0;
  }());
}

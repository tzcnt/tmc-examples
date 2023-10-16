// Demonstrate the timer / delay capabilities of Asio executor
// Use resume_on() to send the task back and forth
// between asio_executor() and cpu_executor()

#define TMC_IMPL
#include "tmc/all_headers.hpp"
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include <atomic>
#include <iostream>

int main() {
  tmc::asio_executor().init();
  return async_main([]() -> tmc::task<int> {
    // Uncomment this to spawn 1000000 tasks and observe the RAM usage
    // co_await tmc::spawn_many(
    //   tmc::iter_adapter(
    //     0,
    //     [](size_t i) -> tmc::task<void> {
    //       co_await asio::steady_timer{
    //         tmc::asio_executor(), std::chrono::seconds(20)}
    //         .async_wait(tmc::aw_asio);
    //       co_return;
    //     }
    //   ),
    //   1000000
    // );

    std::cout << tmc::detail::this_thread::thread_name << std::endl;
    std::cout.flush();
    for (size_t i = 0; i < 8; ++i) {
      auto [error] =
        co_await asio::steady_timer{
          tmc::asio_executor(), std::chrono::milliseconds(250)}
          .async_wait(tmc::aw_asio)
          .resume_on(tmc::asio_executor());
      std::cout << tmc::detail::this_thread::thread_name << std::endl;
      std::cout.flush();
      // co_await tmc::delay(std::chrono::milliseconds(250));
      co_await asio::steady_timer{
        tmc::asio_executor(), std::chrono::milliseconds(250)}
        .async_wait(tmc::aw_asio)
        .resume_on(tmc::cpu_executor());
      std::cout << tmc::detail::this_thread::thread_name << std::endl;
      std::cout.flush();
    }
    co_await resume_on(tmc::cpu_executor());
    co_return 0;
  }());
}

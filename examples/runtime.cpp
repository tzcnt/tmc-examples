#define TMC_IMPL
#include "tmc/asio/aw_asio.hpp"
#include "tmc/asio/ex_asio.hpp"
#include "tmc/aw_resume_on.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include <atomic>
#include <iostream>

int main() {
  tmc::asio_executor().init();
  return async_main([]() -> tmc::task<int> {
    co_await tmc::spawn_many(tmc::iter_adapter(0,
                                               [](size_t i) -> tmc::task<void> {
                                                 co_await asio::steady_timer{
                                                     tmc::asio_executor(),
                                                     std::chrono::seconds(20)}
                                                     .async_wait(tmc::aw_asio);
                                                 co_return;
                                               }),
                             0, 1000000);

    std::cout << tmc::detail::this_thread::thread_name << std::endl;
    std::cout.flush();
    for (size_t i = 0; i < 4; ++i) {
      auto [x] = co_await asio::steady_timer{tmc::asio_executor(),
                                             std::chrono::milliseconds(250)}
                     .async_wait(tmc::aw_asio)
                     .resume_on(tmc::asio_executor());
      std::cout << tmc::detail::this_thread::thread_name << std::endl;
      std::cout.flush();
      // co_await tmc::delay(std::chrono::milliseconds(250));
      co_await asio::steady_timer{tmc::asio_executor(),
                                  std::chrono::milliseconds(250)}
          .async_wait(tmc::aw_asio)
          .resume_on(tmc::cpu_executor());
      std::cout << tmc::detail::this_thread::thread_name << std::endl;
      std::cout.flush();
    }
    co_await resume_on(tmc::cpu_executor());
    co_return 0;
  }());
}

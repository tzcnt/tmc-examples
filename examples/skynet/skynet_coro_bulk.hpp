#pragma once
#include "tmc/all_headers.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <memory_resource>
#include <vector>

template <std::size_t Size, typename T = std::byte> class scoped_buffer {
public:
  using value_type = T;
  using allocator_type = std::pmr::polymorphic_allocator<value_type>;

  constexpr scoped_buffer() noexcept
      : _buffer(), _mbr(_buffer.data(), _buffer.size()), _pa(&_mbr) {}

  constexpr allocator_type& allocator() noexcept { return _pa; }

private:
  std::array<value_type, Size> _buffer;
  std::pmr::monotonic_buffer_resource _mbr;
  allocator_type _pa;
};

namespace skynet {
namespace coro {
namespace bulk {
static std::atomic_bool done;
// all tasks are spawned at the same priority
template <size_t DepthMax>
tmc::task<size_t>
skynet_one(size_t BaseNum, size_t Depth, std::pmr::polymorphic_allocator<>&) {
  if (Depth == DepthMax) {
    co_return BaseNum;
  }
  size_t count = 0;
  size_t depthOffset = 1;
  for (size_t i = 0; i < DepthMax - Depth - 1; ++i) {
    depthOffset *= 10;
  }

  /// Simplest way to spawn subtasks
  // std::array<tmc::task<size_t>, 10> children;
  // for (size_t idx = 0; idx < 10; ++idx) {
  //   children[idx] = skynet_one<DepthMax>(BaseNum + depthOffset * idx,
  //   Depth + 1);
  // }
  // std::array<size_t, 10> results = co_await
  // tmc::spawn_many<10>(children.data());

  auto buffer = scoped_buffer<4096>{};
  /// Concise and slightly faster way to run subtasks
  std::array<size_t, 10> results =
    co_await tmc::spawn_many<10>(tmc::iter_adapter(
      0ULL,
      [=, &buffer](size_t idx) -> tmc::task<size_t> {
        return skynet_one<DepthMax>(
          BaseNum + depthOffset * idx, Depth + 1, buffer.allocator()
        );
      }
    ));

  for (size_t idx = 0; idx < 10; ++idx) {
    count += results[idx];
  }
  co_return count;
}
template <size_t DepthMax> tmc::task<void> skynet() {
  auto buffer = scoped_buffer<4096>{};
  size_t count = co_await skynet_one<DepthMax>(0, 0, buffer.allocator());
  if (count != 499999500000) {
    std::printf("%" PRIu64 "\n", count);
  }
  done.store(true);
}

template <size_t Depth = 6> void run_skynet() {
  done.store(false);
  tmc::ex_cpu executor;
  executor.init();
  auto startTime = std::chrono::high_resolution_clock::now();
  auto future = tmc::post_waitable(executor, skynet<Depth>(), 0);
  future.wait();
  auto endTime = std::chrono::high_resolution_clock::now();
  if (!done.load()) {
    std::printf("skynet_coro_bulk did not finish!\n");
  }

  auto execDur =
    std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime);
  std::printf(
    "executed skynet in %" PRIu64 " ns: %" PRIu64 " thread-ns\n",
    execDur.count(),
    executor.thread_count() * static_cast<size_t>(execDur.count())
  );
}

} // namespace bulk
} // namespace coro
} // namespace skynet

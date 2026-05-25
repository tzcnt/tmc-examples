#pragma once

#include <chrono>
#include <cstddef>
#include <thread>

namespace tmc::tests {

// Helper class for accessing the private `waiter_count()` method on TMC
// synchronization primitives. The primitives friend this class so tests can
// observe waiter counts without exposing the API publicly.
class waiter_count_accessor {
public:
  // Access wrapper for the primitive's private waiter_count().
  template <typename Primitive> static size_t waiter_count(Primitive& P) {
    return P.waiter_count();
  }

  // Polls until the primitive's waiter_count() equals Expected. Used in
  // place of fixed std::this_thread::sleep_for waits, which are not reliable
  // on loaded CI runners.
  template <typename Primitive>
  static void wait_for_waiter_count(Primitive& P, size_t Expected) {
    while (P.waiter_count() != Expected) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
};

} // namespace tmc::tests

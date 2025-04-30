#include "test_common.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#define CATEGORY test_misc

static constexpr size_t Count = 100;
static constexpr size_t PriorityCount = 16;

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor()
      .set_thread_count(4)
      .set_priority_count(PriorityCount)
      .init();
  }
  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, yield) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Dispatch lowest prio -> highest prio so that each task is interrupted
    std::array<size_t, Count> data;
    for (size_t i = 0; i < Count; ++i) {
      data[i] = 0;
    }
    std::array<std::future<void>, Count> results;
    size_t slot = 0;
    for (size_t slot = 0; slot < Count;) {
      for (size_t prio = PriorityCount - 1; prio != static_cast<size_t>(-1);
           --prio) {
        results[slot] = tmc::post_waitable(
          ex(),
          [](size_t* DataSlot, [[maybe_unused]] size_t Priority)
            -> tmc::task<void> {
            unsigned int a = 0;
            unsigned int b = 1;
            for (unsigned int i = 0; i < 1000; ++i) {
              for (unsigned int j = 0; j < 500; ++j) {
                a = a + b;
                b = b + a;
              }
              if (tmc::yield_requested()) {
                co_await tmc::yield();
              }
            }

            *DataSlot = b;
          }(&data[slot], prio),
          prio
        );
        slot++;
        if (slot == Count) {
          goto DONE;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }
  DONE:
    for (auto& f : results) {
      f.wait();
    }
    co_return;
  }());
}

TEST_F(CATEGORY, yield_if_requested) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Dispatch lowest prio -> highest prio so that each task is interrupted
    std::array<size_t, Count> data;
    for (size_t i = 0; i < Count; ++i) {
      data[i] = 0;
    }
    std::array<std::future<void>, Count> results;
    size_t slot = 0;
    for (size_t slot = 0; slot < Count;) {
      for (size_t prio = PriorityCount - 1; prio != static_cast<size_t>(-1);
           --prio) {
        results[slot] = tmc::post_waitable(
          ex(),
          [](size_t* DataSlot, [[maybe_unused]] size_t Priority)
            -> tmc::task<void> {
            unsigned int a = 0;
            unsigned int b = 1;
            for (unsigned int i = 0; i < 1000; ++i) {
              for (unsigned int j = 0; j < 500; ++j) {
                a = a + b;
                b = b + a;
              }
              co_await tmc::yield_if_requested();
            }

            *DataSlot = b;
          }(&data[slot], prio),
          prio
        );
        slot++;
        if (slot == Count) {
          goto DONE;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }
  DONE:
    for (auto& f : results) {
      f.wait();
    }
    co_return;
  }());
}

TEST_F(CATEGORY, yield_counter) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Dispatch lowest prio -> highest prio so that each task is interrupted
    std::array<size_t, Count> data;
    for (size_t i = 0; i < Count; ++i) {
      data[i] = 0;
    }
    std::array<std::future<void>, Count> results;
    size_t slot = 0;
    for (size_t slot = 0; slot < Count;) {
      for (size_t prio = PriorityCount - 1; prio != static_cast<size_t>(-1);
           --prio) {
        results[slot] = tmc::post_waitable(
          ex(),
          [](size_t* DataSlot, [[maybe_unused]] size_t Priority)
            -> tmc::task<void> {
            unsigned int a = 0;
            unsigned int b = 1;
            auto yield_check = tmc::check_yield_counter<1000>();
            yield_check.reset();
            for (unsigned int i = 0; i < 1000; ++i) {
              for (unsigned int j = 0; j < 500; ++j) {
                a = a + b;
                b = b + a;
              }
              co_await yield_check;
            }

            *DataSlot = b;
          }(&data[slot], prio),
          prio
        );
        slot++;
        if (slot == Count) {
          goto DONE;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }
  DONE:
    for (auto& f : results) {
      f.wait();
    }
    co_return;
  }());
}

TEST_F(CATEGORY, yield_counter_dynamic) {
  test_async_main(ex(), []() -> tmc::task<void> {
    // Dispatch lowest prio -> highest prio so that each task is interrupted
    std::array<size_t, Count> data;
    for (size_t i = 0; i < Count; ++i) {
      data[i] = 0;
    }
    std::array<std::future<void>, Count> results;
    size_t slot = 0;
    for (size_t slot = 0; slot < Count;) {
      for (size_t prio = PriorityCount - 1; prio != static_cast<size_t>(-1);
           --prio) {
        results[slot] = tmc::post_waitable(
          ex(),
          [](size_t* DataSlot, [[maybe_unused]] size_t Priority)
            -> tmc::task<void> {
            unsigned int a = 0;
            unsigned int b = 1;
            auto yield_check = tmc::check_yield_counter_dynamic(1000);
            yield_check.reset();
            for (unsigned int i = 0; i < 1000; ++i) {
              for (unsigned int j = 0; j < 500; ++j) {
                a = a + b;
                b = b + a;
              }
              co_await yield_check;
            }

            *DataSlot = b;
          }(&data[slot], prio),
          prio
        );
        slot++;
        if (slot == Count) {
          goto DONE;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(1));
      }
    }
  DONE:
    for (auto& f : results) {
      f.wait();
    }
    co_return;
  }());
}

#undef CATEGORY

#include "test_common.hpp"

#include <gtest/gtest.h>

#define CATEGORY ex_cpu

TEST(CATEGORY, TMC) {
  tmc::ex_cpu ex;
  ex.set_thread_count(1).init();
  tmc::post_waitable(ex, []() -> tmc::task<void> { co_return; }(), 0).wait();
}
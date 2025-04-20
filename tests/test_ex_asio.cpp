#include "tmc/asio/ex_asio.hpp"

#include <gtest/gtest.h>

#include <atomic>

#define CATEGORY test_ex_asio

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::asio_executor().init(); }

  static void TearDownTestSuite() { tmc::asio_executor().teardown(); }

  static tmc::ex_asio& ex() { return tmc::asio_executor(); }
};

TEST_F(CATEGORY, set_thread_init_hook) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  {
    tmc::ex_asio ex;
    ex.set_thread_init_hook([&](size_t tid) -> void { thr = tid; }).init();
  }
  EXPECT_EQ(thr.load(), 0);
}

TEST_F(CATEGORY, set_thread_teardown_hook) {
  std::atomic<size_t> thr = TMC_ALL_ONES;
  {
    tmc::ex_asio ex;
    ex.set_thread_teardown_hook([&](size_t tid) -> void { thr = tid; }).init();
  }
  EXPECT_EQ(thr.load(), 0);
}

TEST_F(CATEGORY, no_init) { tmc::ex_asio ex; }

TEST_F(CATEGORY, init_twice) {
  tmc::ex_asio ex;
  ex.init();
  ex.init();
}

TEST_F(CATEGORY, teardown_twice) {
  tmc::ex_asio ex;
  ex.teardown();
  ex.teardown();
}

TEST_F(CATEGORY, teardown_and_destroy) {
  tmc::ex_asio ex;
  ex.teardown();
}

#include "test_executors.ipp"
#include "test_nested_executors.ipp"
#include "test_spawn_composition.ipp"
#include "test_spawn_func_many.ipp"
#include "test_spawn_func_many_detach.ipp"
#include "test_spawn_func_many_each.ipp"
#include "test_spawn_func_many_fork.ipp"
#include "test_spawn_many.ipp"
#include "test_spawn_many_detach.ipp"
#include "test_spawn_many_each.ipp"
#include "test_spawn_many_fork.ipp"
#include "test_spawn_tuple.ipp"

#undef CATEGORY

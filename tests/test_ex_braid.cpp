#include "test_common.hpp"
#include "tmc/detail/thread_locals.hpp"
#include "tmc/ex_cpu.hpp"

#include <gtest/gtest.h>
#include <optional>

#define CATEGORY test_ex_braid

class CATEGORY : public testing::Test {
protected:
  static inline std::optional<tmc::ex_braid> braid;
  static void SetUpTestSuite() {
    tmc::cpu_executor().init();
    braid.emplace(tmc::cpu_executor());
  }

  static void TearDownTestSuite() {
    braid.reset();
    tmc::cpu_executor().teardown();
  }

  static tmc::ex_braid& ex() { return *braid; }
};

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

TEST_F(CATEGORY, destroy_running_braid) {
  test_async_main(ex(), []() -> tmc::task<void> {
    tmc::ex_braid br;
    co_await tmc::enter(br);
    EXPECT_EQ(tmc::detail::this_thread::executor, br.type_erased());
    // The braid will be destroyed at the end of this coroutine.
    // Afterward, the coroutine will return, and the call stack will be inside
    // of try_run_loop, a member function of the destroyed braid.
    // A separately allocated boolean is used to track when this occurs and exit
    // the runloop safely.
  }());
}

#undef CATEGORY

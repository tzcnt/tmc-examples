#include "test_common.hpp"
#include "test_spawn_many_common.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <ranges>
#include <vector>

// tests ported from examples/spawn_iterator.cpp

static inline tmc::task<void> spawn_tuple_compose() {
  // These types aren't move-constructible directly into the spawn_tuple,
  // since they initiate their operations immediately.
  // spawn_tuple is able to take lvalues to these, pass them to safe_wrap
  // which creates an awaiting task. spawn_tuple also allows passing lvalue for
  // a task, which is something to fix later. It's not broken, but it goes
  // against the linear types goal.
  auto sre = tmc::spawn(work(2)).run_early();
  auto tre = tmc::spawn_tuple(work(4)).run_early();
  auto smare = tmc::spawn_many<1>(tmc::iter_adapter(6, work)).run_early();
  auto smvre = tmc::spawn_many(tmc::iter_adapter(8, work), 1).run_early();

  std::tuple<
    int, int, int, std::tuple<int>, std::tuple<int>, std::array<int, 1>,
    std::array<int, 1>, std::vector<int>, std::vector<int>>
    results = co_await tmc::spawn_tuple(
      work(0), tmc::spawn(work(1)), sre, tmc::spawn_tuple(work(3)), tre,
      tmc::spawn_many<1>(tmc::iter_adapter(5, work)), smare,
      tmc::spawn_many(tmc::iter_adapter(7, work), 1), smvre
    );

  auto sum = std::get<0>(results) + std::get<1>(results) +
             std::get<2>(results) + std::get<0>(std::get<3>(results)) +
             std::get<0>(std::get<4>(results)) + std::get<5>(results)[0] +
             std::get<6>(results)[0] + std::get<7>(results)[0] +
             std::get<8>(results)[0];

  EXPECT_EQ(sum, (1 << 9) - 1);
}

static inline tmc::task<void> spawn_tuple_compose_void() {
  std::array<int, 9> results{0, 1, 2, 3, 4, 5, 6, 7, 8};
  auto set = [](int& i) -> tmc::task<void> {
    i = (1 << i);
    co_return;
  };

  // These types aren't move-constructible directly into the spawn_tuple,
  // since they initiate their operations immediately.
  // spawn_tuple is able to take lvalues to these, pass them to safe_wrap
  // which creates an awaiting task. spawn_tuple also allows passing lvalue for
  // a task, which is something to fix later. It's not broken, but it goes
  // against the linear types goal.
  auto sre = tmc::spawn(set(results[2])).run_early();
  auto tre = tmc::spawn_tuple(set(results[4])).run_early();
  auto t6 = set(results[6]);
  auto smare = tmc::spawn_many<1>(&t6).run_early();
  auto t8 = set(results[8]);
  auto smvre = tmc::spawn_many(&t8, 1).run_early();

  auto t5 = set(results[5]);
  auto t7 = set(results[7]);
  std::tuple<
    std::monostate, std::monostate, std::monostate, std::tuple<std::monostate>,
    std::tuple<std::monostate>, std::monostate, std::monostate, std::monostate,
    std::monostate, std::tuple<>>
    foo = co_await tmc::spawn_tuple(
      set(results[0]), tmc::spawn(set(results[1])), sre,
      tmc::spawn_tuple(set(results[3])), tre, tmc::spawn_many<1>(&t5), smare,
      tmc::spawn_many(&t7, 1), smvre,
      // throw in an empty tuple at the end
      tmc::spawn_tuple()
    );
  auto sum = std::accumulate(results.begin(), results.end(), 0);

  EXPECT_EQ(sum, (1 << 9) - 1);
}

TEST_F(CATEGORY, spawn_tuple_compose) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_compose();
  }());
}

TEST_F(CATEGORY, spawn_tuple_compose_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_compose_void();
  }());
}

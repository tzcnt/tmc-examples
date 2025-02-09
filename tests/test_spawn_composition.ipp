#include "test_common.hpp"
#include "test_spawn_many_common.hpp"
#include "tmc/utils.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <ranges>
#include <vector>

static inline tmc::task<void> spawn_tuple_compose() {
  // These run_early() types aren't move-constructible directly into the
  // spawn_tuple, since they initiate their operations immediately. spawn_tuple
  // is able to take lvalues to these, pass them to safe_wrap which creates an
  // awaiting task. spawn_tuple also allows passing lvalue for a task, which is
  // something to fix later. This works and produces the correct output, but is
  // undesirable because it does not implement the linear type rules.
  auto sre = tmc::spawn(work(2)).run_early();
  auto tre = tmc::spawn_tuple(work(4)).run_early();
  auto smare = tmc::spawn_many<1>(tmc::iter_adapter(6, work)).run_early();
  auto smvre = tmc::spawn_many(tmc::iter_adapter(8, work), 1).run_early();
  auto sfre = tmc::spawn_func([]() -> int { return 1 << 10; }).run_early();
  auto sfmare =
    tmc::spawn_func_many<1>(
      tmc::iter_adapter(
        12, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
      )
    ).run_early();
  auto sfmvre =
    tmc::spawn_func_many(
      tmc::iter_adapter(
        14, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
      ),
      1
    )
      .run_early();

  std::tuple<
    int, int, int, std::tuple<int>, std::tuple<int>, std::array<int, 1>,
    std::array<int, 1>, std::vector<int>, std::vector<int>, int, int,
    std::array<int, 1>, std::array<int, 1>, std::vector<int>, std::vector<int>>
    results = co_await tmc::spawn_tuple(
      work(0), tmc::spawn(work(1)), sre, tmc::spawn_tuple(work(3)), tre,
      tmc::spawn_many<1>(tmc::iter_adapter(5, work)), smare,
      tmc::spawn_many(tmc::iter_adapter(7, work), 1), smvre,
      tmc::spawn_func([]() -> int { return 1 << 9; }), sfre,
      tmc::spawn_func_many<1>(tmc::iter_adapter(
        11, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
      )),
      sfmare,
      tmc::spawn_func_many(
        tmc::iter_adapter(
          13, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
        ),
        1
      ),
      sfmvre
    );

  auto sum = std::get<0>(results) + std::get<1>(results) +
             std::get<2>(results) + std::get<0>(std::get<3>(results)) +
             std::get<0>(std::get<4>(results)) + std::get<5>(results)[0] +
             std::get<6>(results)[0] + std::get<7>(results)[0] +
             std::get<8>(results)[0] + std::get<9>(results) +
             std::get<10>(results) + std::get<11>(results)[0] +
             std::get<12>(results)[0] + std::get<13>(results)[0] +
             std::get<14>(results)[0];

  EXPECT_EQ(sum, (1 << 15) - 1);
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
  // which creates an awaiting task. This is undesirable or unclear behavior.
  // spawn_tuple also allows passing lvalue for a task. This is something to
  // fix later. It's not broken, but it fails to implement the linear type
  // rules.
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

static inline tmc::task<void> spawn_many_compose_spawn() {
  {
    std::array<tmc::aw_spawned_task<tmc::task<int>>, 2> ts{
      tmc::spawn(work(0)), tmc::spawn(work(1))
    };
    std::array<int, 2> results = co_await tmc::spawn_many<2>(ts.data());

    auto sum = results[0] + results[1];

    EXPECT_EQ(sum, (1 << 2) - 1);
  }
  {
    std::array<int, 2> void_results{0, 1};
    auto set = [](int& i) -> tmc::task<void> {
      i = (1 << i);
      co_return;
    };
    std::array<tmc::aw_spawned_task<tmc::task<void>>, 2> ts{
      tmc::spawn(set(void_results[0])), tmc::spawn(set(void_results[1]))
    };
    co_await tmc::spawn_many<2>(ts.data());

    auto sum = void_results[0] + void_results[1];

    EXPECT_EQ(sum, (1 << 2) - 1);
  }
}

static inline tmc::task<void> spawn_many_compose_spawn_many() {
  {
    // This version segfaults. Inner range holds dangling reference?
    // auto iter =
    //   std::ranges::views::iota(0) | std::ranges::views::transform([](int i) {
    //     return tmc::spawn_many<2>((std::ranges::views::iota(i * 2) |
    //                                std::ranges::views::transform(work))
    //                                 .begin());
    //   });

    // Using iter_adapter instead of inner range works.
    auto iter =
      std::ranges::views::iota(0) | std::ranges::views::transform([](int i) {
        return tmc::spawn_many<2>(tmc::iter_adapter(i * 2, work));
      });
    std::array<std::array<int, 2>, 2> results =
      co_await tmc::spawn_many<2>(iter.begin());
    auto sum = results[0][0] + results[0][1] + results[1][0] + results[1][1];
    EXPECT_EQ(sum, (1 << 4) - 1);
  }
  {
    std::array<int, 4> void_results{0, 1, 2, 3};
    auto set = [](int* i) -> tmc::task<void> {
      *i = (1 << *i);
      co_return;
    };

    auto iter =
      std::ranges::views::iota(0) | std::ranges::views::transform([&](int i) {
        return tmc::spawn_many<2>(
          (std::ranges::views::iota(void_results.data() + (i * 2)) |
           std::ranges::views::transform(set))
            .begin()
        );
      });
    co_await tmc::spawn_many<2>(iter.begin());
    auto sum = std::accumulate(void_results.begin(), void_results.end(), 0);
    EXPECT_EQ(sum, (1 << 4) - 1);
  }
}

static inline tmc::task<void> spawn_many_compose_spawn_func() {
  {
    std::array<tmc::aw_spawned_func<int>, 2> ts{
      tmc::spawn_func([]() -> int { return 1 << 0; }),
      tmc::spawn_func([]() -> int { return 1 << 1; })
    };
    std::array<int, 2> results = co_await tmc::spawn_many<2>(ts.data());

    auto sum = results[0] + results[1];

    EXPECT_EQ(sum, (1 << 2) - 1);
  }
  {
    std::array<int, 2> void_results{0, 1};
    auto set = [](int& i) -> tmc::task<void> {
      i = (1 << i);
      co_return;
    };
    std::array<tmc::aw_spawned_func<void>, 2> ts{
      tmc::spawn_func([&void_results]() -> void { void_results[0] = 1 << 0; }),
      tmc::spawn_func([&void_results]() -> void { void_results[1] = 1 << 1; })
    };
    co_await tmc::spawn_many<2>(ts.data());

    auto sum = void_results[0] + void_results[1];

    EXPECT_EQ(sum, (1 << 2) - 1);
  }
}

static inline tmc::task<void> spawn_many_compose_spawn_func_many() {
  {
    // This version does not segfault when using inner range (compare to
    // spawn_many_compose_spawn_many).
    auto iter =
      std::ranges::views::iota(0) | std::ranges::views::transform([](int i) {
        return tmc::spawn_func_many<2>(
          (std::ranges::views::iota(i * 2) |
           std::ranges::views::transform([](int i) -> auto {
             return [i]() -> int { return 1 << i; };
           })
          ).begin()
        );
      });

    // Using iter_adapter instead of inner range also works.
    // auto iter =
    //   std::ranges::views::iota(0) | std::ranges::views::transform([](int i) {
    //     return tmc::spawn_func_many<2>(tmc::iter_adapter(
    //       i * 2, [](int i) -> auto { return [i]() -> int { return 1 << i; };
    //       }
    //     ));
    //   });

    std::array<std::array<int, 2>, 2> results =
      co_await tmc::spawn_many<2>(iter.begin());
    auto sum = results[0][0] + results[0][1] + results[1][0] + results[1][1];
    EXPECT_EQ(sum, (1 << 4) - 1);
  }
  {
    std::array<int, 4> void_results{0, 1, 2, 3};
    auto iter =
      std::ranges::views::iota(0) | std::ranges::views::transform([&](int i) {
        return tmc::spawn_func_many<2>(
          (std::ranges::views::iota(void_results.data() + (i * 2)) |
           std::ranges::views::transform([](int* i) -> auto {
             return [i]() { *i = (1 << *i); };
           })
          ).begin()
        );
      });
    co_await tmc::spawn_many<2>(iter.begin());
    auto sum = std::accumulate(void_results.begin(), void_results.end(), 0);
    EXPECT_EQ(sum, (1 << 4) - 1);
  }
}

static inline tmc::task<void> spawn_many_compose_tuple() {
  std::array<int, 2> void_results{1, 3};
  auto set = [](int& i) -> tmc::task<void> {
    i = (1 << i);
    co_return;
  };
  std::array<tmc::aw_spawned_task_tuple<tmc::task<int>, tmc::task<void>>, 2> ts{
    tmc::spawn_tuple(work(0), set(void_results[0])),
    tmc::spawn_tuple(work(2), set(void_results[1])),
  };
  std::array<std::tuple<int, std::monostate>, 2> results =
    co_await tmc::spawn_many<2>(ts.data());

  auto sum = std::get<0>(results[0]) + std::get<0>(results[1]) +
             void_results[0] + void_results[1];

  EXPECT_EQ(sum, (1 << 4) - 1);
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

TEST_F(CATEGORY, spawn_many_compose_spawn) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_compose_spawn();
  }());
}

TEST_F(CATEGORY, spawn_many_compose_spawn_many) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_compose_spawn_many();
  }());
}

TEST_F(CATEGORY, spawn_many_compose_spawn_func) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_compose_spawn_func();
  }());
}

TEST_F(CATEGORY, spawn_many_compose_spawn_func_many) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_compose_spawn_func_many();
  }());
}

TEST_F(CATEGORY, spawn_many_compose_tuple) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_many_compose_tuple();
  }());
}

// Doesn't compile - as expected. run_early() types cannot be moved
// static inline tmc::task<void> spawn_many_compose_run_early() {
//   {
//     std::array<tmc::aw_run_early<tmc::task<int>>, 2> ts{
//       tmc::spawn(work(0)).run_early(), tmc::spawn(work(1)).run_early()
//     };
//     std::array<int, 2> results =
//       co_await tmc::spawn_many<2>(std::move(ts).data());

//     auto sum = results[0] + results[1];

//     EXPECT_EQ(sum, (1 << 2) - 1);
//   }
//   {
//     std::array<int, 2> void_results{0, 1};
//     auto set = [](int& i) -> tmc::task<void> {
//       i = (1 << i);
//       co_return;
//     };
//     std::array<tmc::aw_run_early<tmc::task<void>>, 2> ts{
//       tmc::spawn(set(void_results[0])).run_early(),
//       tmc::spawn(set(void_results[1])).run_early()
//     };
//     co_await tmc::spawn_many<2>(ts.data());

//     auto sum = void_results[0] + void_results[1];

//     EXPECT_EQ(sum, (1 << 2) - 1);
//   }
// }

// TEST_F(CATEGORY, spawn_many_compose_run_early) {
//   test_async_main(ex(), []() -> tmc::task<void> {
//     co_await spawn_many_compose_run_early();
//   }());
// }
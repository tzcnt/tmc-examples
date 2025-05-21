#include "atomic_awaitable.hpp"
#include "test_common.hpp"
#include "test_spawn_many_common.hpp"
#include "tmc/utils.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <ranges>
#include <vector>

static inline tmc::task<void> spawn_tuple_compose() {
  // These fork() types aren't move-constructible directly into the
  // spawn_tuple, since they initiate their operations immediately. However,
  // they can still be awaited by passing rvalue reference parameters to
  // spawn_tuple (as if they were being co_awaited).
  auto sf = tmc::spawn(work(2)).fork();
  auto tf = tmc::spawn_tuple(work(4)).fork();
  auto smaf = tmc::spawn_many<1>(tmc::iter_adapter(6, work)).fork();
  auto smvf = tmc::spawn_many(tmc::iter_adapter(8, work), 1).fork();
  auto sff = tmc::spawn_func([]() -> int { return 1 << 10; }).fork();
  auto sfmaf =
    tmc::spawn_func_many<1>(
      tmc::iter_adapter(
        12, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
      )
    ).fork();
  auto sfmvf =
    tmc::spawn_func_many(
      tmc::iter_adapter(
        14, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
      ),
      1
    )
      .fork();

  std::tuple<
    int, int, int, std::tuple<int>, std::tuple<int>, std::array<int, 1>,
    std::array<int, 1>, std::vector<int>, std::vector<int>, int, int,
    std::array<int, 1>, std::array<int, 1>, std::vector<int>, std::vector<int>>
    results = co_await tmc::spawn_tuple(
      work(0), tmc::spawn(work(1)), std::move(sf), tmc::spawn_tuple(work(3)),
      std::move(tf), tmc::spawn_many<1>(tmc::iter_adapter(5, work)),
      std::move(smaf), tmc::spawn_many(tmc::iter_adapter(7, work), 1),
      std::move(smvf), tmc::spawn_func([]() -> int { return 1 << 9; }),
      std::move(sff),
      tmc::spawn_func_many<1>(tmc::iter_adapter(
        11, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
      )),
      std::move(sfmaf),
      tmc::spawn_func_many(
        tmc::iter_adapter(
          13, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
        ),
        1
      ),
      std::move(sfmvf)
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

  // These fork() types aren't move-constructible directly into the
  // spawn_tuple, since they initiate their operations immediately. However,
  // they can still be awaited by passing rvalue reference parameters to
  // spawn_tuple (as if they were being co_awaited).
  auto sf = tmc::spawn(set(results[2])).fork();
  auto tf = tmc::spawn_tuple(set(results[4])).fork();
  auto t6 = set(results[6]);
  auto smaf = tmc::spawn_many<1>(&t6).fork();
  auto t8 = set(results[8]);
  auto smvf = tmc::spawn_many(&t8, 1).fork();

  auto t5 = set(results[5]);
  auto t7 = set(results[7]);
  std::tuple<
    std::monostate, std::monostate, std::monostate, std::tuple<std::monostate>,
    std::tuple<std::monostate>, std::monostate, std::monostate, std::monostate,
    std::monostate, std::tuple<>>
    foo = co_await tmc::spawn_tuple(
      set(results[0]), tmc::spawn(set(results[1])), std::move(sf),
      tmc::spawn_tuple(set(results[3])), std::move(tf), tmc::spawn_many<1>(&t5),
      std::move(smaf), tmc::spawn_many(&t7, 1), std::move(smvf),
      tmc::spawn_tuple()
    );
  auto sum = std::accumulate(results.begin(), results.end(), 0);

  EXPECT_EQ(sum, (1 << 9) - 1);
}

static inline tmc::task<void> spawn_tuple_compose_void_detach() {
  atomic_awaitable<int> aa(4);
  std::array<int, 4> results{0, 1, 2, 3};
  auto set = [](int& i, atomic_awaitable<int>& AA) -> tmc::task<void> {
    i = (1 << i);

    AA.inc();
    co_return;
  };

  // Underspecified behavior - spawn_many allows to move from these tasks as
  // arrays without requiring an explicit move cast.
  auto t2 = set(results[2], aa);
  auto t3 = set(results[3], aa);

  tmc::spawn_tuple(
    set(results[0], aa), tmc::spawn(set(results[1], aa)),
    tmc::spawn_many<1>(&t2), tmc::spawn_many(&t3, 1)
  )
    .detach();

  co_await aa;

  auto sum = std::accumulate(results.begin(), results.end(), 0);
  EXPECT_EQ(sum, (1 << 4) - 1);

  co_return;
}

static inline tmc::task<void> spawn_many_compose_spawn() {
  {
    std::array<tmc::aw_spawn<tmc::task<int>>, 2> ts{
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
    std::array<tmc::aw_spawn<tmc::task<void>>, 2> ts{
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
    std::array<tmc::aw_spawn_func<int>, 2> ts{
      tmc::spawn_func([]() -> int { return 1 << 0; }),
      tmc::spawn_func([]() -> int { return 1 << 1; })
    };
    std::array<int, 2> results = co_await tmc::spawn_many<2>(ts.data());

    auto sum = results[0] + results[1];

    EXPECT_EQ(sum, (1 << 2) - 1);
  }
  {
    std::array<int, 2> void_results{0, 1};
    std::array<tmc::aw_spawn_func<void>, 2> ts{
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
           std::ranges::views::transform([](int j) -> auto {
             return [j]() -> int { return 1 << j; };
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
           std::ranges::views::transform([](int* j) -> auto {
             return [j]() { *j = (1 << *j); };
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
  std::array<tmc::aw_spawn_tuple<tmc::task<int>, tmc::task<void>>, 2> ts{
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

TEST_F(CATEGORY, spawn_tuple_compose_void_detach) {
  test_async_main(ex(), []() -> tmc::task<void> {
    co_await spawn_tuple_compose_void_detach();
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

TEST_F(CATEGORY, spawn_compose) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      auto [x] = co_await tmc::spawn(tmc::spawn_tuple(work(1)));
      EXPECT_EQ(x, 2);
    }
    {
      auto t = tmc::spawn(tmc::spawn_tuple(work(1))).fork();
      auto [x] = co_await std::move(t);
      EXPECT_EQ(x, 2);
    }
  }());
}

TEST_F(CATEGORY, spawn_compose_void) {
  test_async_main(ex(), []() -> tmc::task<void> {
    auto set = [](int& i) -> tmc::task<void> {
      i = (1 << i);
      co_return;
    };
    {
      int i = 1;
      auto [x] = co_await tmc::spawn(tmc::spawn_tuple(set(i)));
      EXPECT_EQ(i, 2);
    }
    {
      int i = 1;
      auto t = tmc::spawn(tmc::spawn_tuple(set(i))).fork();
      auto [x] = co_await std::move(t);
      EXPECT_EQ(i, 2);
    }
    {
      atomic_awaitable<int> aa(1);
      tmc::spawn(tmc::spawn([](atomic_awaitable<int>& AA) -> tmc::task<void> {
        AA.inc();
        co_return;
      }(aa)))
        .detach();
      co_await aa;
    }
  }());
}

// Like spawn_tuple, spawn is allowed to accept lvalues if the type being passed
// in doesn't have a move constructor.
TEST_F(CATEGORY, spawn_compose_lvalues) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      auto t = tmc::spawn_tuple(work(1)).fork();
      auto [x] = co_await tmc::spawn(std::move(t));
      EXPECT_EQ(x, 2);
    }
    {
      auto tt = tmc::spawn_tuple(work(1)).fork();
      auto t = tmc::spawn(std::move(tt)).fork();
      auto [x] = co_await tmc::spawn(std::move(t));
      EXPECT_EQ(x, 2);
    }
  }());
}

TEST_F(CATEGORY, spawn_many_compose_spawn_fork) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      std::array<tmc::aw_spawn_fork<tmc::task<int>>, 2> ts{
        tmc::spawn(work(0)).fork(), tmc::spawn(work(1)).fork()
      };
      std::array<int, 2> results =
        co_await tmc::spawn_many<2>(std::move(ts).data());

      auto sum = results[0] + results[1];

      EXPECT_EQ(sum, (1 << 2) - 1);
    }
    {
      std::array<int, 2> void_results{0, 1};
      auto set = [](int& i) -> tmc::task<void> {
        i = (1 << i);
        co_return;
      };
      std::array<tmc::aw_spawn_fork<tmc::task<void>>, 2> ts{
        tmc::spawn(set(void_results[0])).fork(),
        tmc::spawn(set(void_results[1])).fork()
      };
      co_await tmc::spawn_many<2>(ts.data());

      auto sum = void_results[0] + void_results[1];

      EXPECT_EQ(sum, (1 << 2) - 1);
    }
  }());
}

TEST_F(CATEGORY, spawn_many_compose_spawn_tuple_fork) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      std::array<tmc::aw_spawn_tuple_fork<tmc::task<int>>, 2> ts{
        tmc::spawn_tuple(work(0)).fork(), tmc::spawn_tuple(work(1)).fork()
      };
      std::array<std::tuple<int>, 2> results =
        co_await tmc::spawn_many<2>(std::move(ts).data());

      auto sum = std::get<0>(results[0]) + std::get<0>(results[1]);

      EXPECT_EQ(sum, (1 << 2) - 1);
    }
    {
      std::array<int, 2> void_results{0, 1};
      auto set = [](int& i) -> tmc::task<void> {
        i = (1 << i);
        co_return;
      };
      std::array<tmc::aw_spawn_tuple_fork<tmc::task<void>>, 2> ts{
        tmc::spawn_tuple(set(void_results[0])).fork(),
        tmc::spawn_tuple(set(void_results[1])).fork()
      };
      co_await tmc::spawn_many<2>(ts.data());

      auto sum = void_results[0] + void_results[1];

      EXPECT_EQ(sum, (1 << 2) - 1);
    }
  }());
}

TEST_F(CATEGORY, multi_level_composition_value) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::task<int> t1 = work(0);
      tmc::task<int> t2 = work(1);

      tmc::aw_spawn<tmc::task<int>> ts1 = tmc::spawn(std::move(t1));
      tmc::aw_spawn<tmc::task<int>> ts2 = tmc::spawn(std::move(t2));

      std::array<tmc::aw_spawn_tuple<tmc::aw_spawn<tmc::task<int>>>, 2> tt{
        tmc::spawn_tuple(std::move(ts1)), tmc::spawn_tuple(std::move(ts2))
      };
      auto tsm = tmc::spawn_many<2>(std::move(tt).data());

      auto whyNot = tmc::spawn(std::move(tsm));
      std::array<std::tuple<int>, 2> results = co_await std::move(whyNot);

      auto sum = std::get<0>(results[0]) + std::get<0>(results[1]);

      EXPECT_EQ(sum, (1 << 2) - 1);
    }
  }());
}

TEST_F(CATEGORY, multi_level_composition_rvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::task<int> t1 = work(0);
      tmc::task<int> t2 = work(1);

      tmc::aw_spawn_fork<tmc::task<int>> ts1 = tmc::spawn(std::move(t1)).fork();
      tmc::aw_spawn_fork<tmc::task<int>> ts2 = tmc::spawn(std::move(t2)).fork();

      std::array<
        tmc::aw_spawn_tuple_fork<tmc::aw_spawn_fork<tmc::task<int>>&&>, 2>
        tt{
          tmc::spawn_tuple(std::move(ts1)).fork(),
          tmc::spawn_tuple(std::move(ts2)).fork()
        };
      auto tsm = tmc::spawn_many<2>(std::move(tt).data()).fork();

      auto whyNot = tmc::spawn(std::move(tsm));
      std::array<std::tuple<int>, 2> results = co_await std::move(whyNot);

      auto sum = std::get<0>(results[0]) + std::get<0>(results[1]);

      EXPECT_EQ(sum, (1 << 2) - 1);
    }
  }());
}

TEST_F(CATEGORY, multi_level_composition_lvalue) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::task<int> t1 = work(0);
      tmc::task<int> t2 = work(1);

      tmc::aw_spawn_tuple_each<tmc::task<int>> te1 =
        tmc::spawn_tuple(std::move(t1)).result_each();
      tmc::aw_spawn_tuple_each<tmc::task<int>> te2 =
        tmc::spawn_tuple(std::move(t2)).result_each();

      // This is kind of an abuse of the rules - the lvalue only awaitable from
      // result_each() is turned into an rvalue only awaitable with spawn()
      // which can then be passed to spawn_many().
      std::array<tmc::aw_spawn<tmc::aw_spawn_tuple_each<tmc::task<int>>&>, 2>
        tt{tmc::spawn(te1), tmc::spawn(te2)};

      // Spawn_many is always movable - however the type in the array that it
      // reads from is not movable, so it should forward the lvalue reference to
      // the safe_wrap awaitable / co_await expression.
      auto tsm = tmc::spawn_many<2>(std::move(tt).data());

      auto whyNot = tmc::spawn(std::move(tsm));
      std::array<size_t, 2> results = co_await std::move(whyNot);

      auto sum = results[0] + results[1];
      EXPECT_EQ(sum, 0);

      // We can't await this again because the spawn_many is a single-use
      // awaitable, even though the underlying result_each() awaitables could be
      // awaited multiple times.
      // results = co_await std::move(whyNot);
      // sum = results[0] + results[1];
      // EXPECT_EQ(sum, 128);
    }
  }());
}

TEST_F(CATEGORY, tuple_compose_each_twice) {
  test_async_main(ex(), []() -> tmc::task<void> {
    {
      tmc::aw_spawn_tuple_each<tmc::task<int>, tmc::task<int>> te1 =
        tmc::spawn_tuple(work(0), work(1)).result_each();
      tmc::aw_spawn_tuple_each<tmc::task<int>, tmc::task<int>> te2 =
        tmc::spawn_tuple(work(2), work(3)).result_each();

      int sum = 0;

      // Get the first result from each inner tuple.
      auto [x, y] = co_await tmc::spawn_tuple(te1, te2);
      switch (x) {
      case 0:
        sum += te1.get<0>();
        break;
      case 1:
        sum += te1.get<1>();
        break;
      default:
        EXPECT_TRUE(false);
      }
      switch (y) {
      case 0:
        sum += te2.get<0>();
        break;
      case 1:
        sum += te2.get<1>();
        break;
      default:
        EXPECT_TRUE(false);
      }

      // Get the second result from each inner tuple.
      std::tie(x, y) = co_await tmc::spawn_tuple(te1, te2);
      switch (x) {
      case 0:
        sum += te1.get<0>();
        break;
      case 1:
        sum += te1.get<1>();
        break;
      default:
        EXPECT_TRUE(false);
      }
      switch (y) {
      case 0:
        sum += te2.get<0>();
        break;
      case 1:
        sum += te2.get<1>();
        break;
      default:
        EXPECT_TRUE(false);
      }

      EXPECT_EQ(sum, (1 << 4) - 1);

      // Get the final result from each inner tuple.
      std::tie(x, y) = co_await tmc::spawn_tuple(te1, te2);

      EXPECT_EQ(x, te1.end());
      EXPECT_EQ(y, te2.end());
    }
  }());
}

//// Doesn't / shouldn't compile - co_awaiting a temporary result_each() is
//// invalid - it must be awaited as an lvalue.
//
// TEST_F(CATEGORY, co_await_lvalue_tuple_each) {
//   test_async_main(ex(), []() -> tmc::task<void> {
//     auto x = co_await tmc::spawn_tuple(work(0)).result_each();
//   }());
// }
//
// TEST_F(CATEGORY, co_await_lvalue_many_each) {
//   test_async_main(ex(), []() -> tmc::task<void> {
//     tmc::task<int> t = work(0);
//     auto x = co_await tmc::spawn_many(&t, 1).result_each();
//   }());
// }

//// Doesn't / shouldn't compile - wrapping a temporary result_each() is invalid
//// - it must be awaited as an lvalue.
//
// TEST_F(CATEGORY, multi_level_composition_lvalue_tuple_each_in_tuple) {
//   test_async_main(ex(), []() -> tmc::task<void> {
//     {
//       tmc::task<int> t1 = work(0);
//       auto tte =
//         tmc::spawn_tuple(tmc::spawn_tuple(std::move(t1)).result_each());
//       co_await std::move(tte);
//     }
//   }());
// }
//
// TEST_F(CATEGORY, multi_level_composition_lvalue_many_each_in_tuple) {
//   test_async_main(ex(), []() -> tmc::task<void> {
//     {
//       tmc::task<int> t1 = work(0);
//       auto tte = tmc::spawn_tuple(tmc::spawn_many(&t1, 1).result_each());
//       co_await std::move(tte);
//     }
//   }());
// }

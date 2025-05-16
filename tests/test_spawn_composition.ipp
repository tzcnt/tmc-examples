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
  // spawn_tuple, since they initiate their operations immediately. As an
  // exception to the linear type rules, spawn_tuple is allowed to take lvalues
  // to these since they don't have a move constructor, and pass them to
  // safe_wrap which creates a task that awaits that lvalue reference.
  auto sre = tmc::spawn(work(2)).fork();
  auto tre = tmc::spawn_tuple(work(4)).fork();
  auto smare = tmc::spawn_many<1>(tmc::iter_adapter(6, work)).fork();
  auto smvre = tmc::spawn_many(tmc::iter_adapter(8, work), 1).fork();
  auto sfre = tmc::spawn_func([]() -> int { return 1 << 10; }).fork();
  auto sfmare =
    tmc::spawn_func_many<1>(
      tmc::iter_adapter(
        12, [](int i) -> auto { return [i]() -> int { return 1 << i; }; }
      )
    ).fork();
  auto sfmvre =
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

  // These fork() types aren't move-constructible directly into the
  // spawn_tuple, since they initiate their operations immediately. As an
  // exception to the linear type rules, spawn_tuple is allowed to take lvalues
  // to these since they don't have a move constructor, and pass them to
  // safe_wrap which creates a task that awaits that lvalue reference.e
  // rules.
  auto sre = tmc::spawn(set(results[2])).fork();
  auto tre = tmc::spawn_tuple(set(results[4])).fork();
  auto t6 = set(results[6]);
  auto smare = tmc::spawn_many<1>(&t6).fork();
  auto t8 = set(results[8]);
  auto smvre = tmc::spawn_many(&t8, 1).fork();

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
      auto [x] = co_await tmc::spawn(t);
      EXPECT_EQ(x, 2);
    }
    {
      auto tt = tmc::spawn_tuple(work(1)).fork();
      auto t = tmc::spawn(tt).fork();
      auto [x] = co_await tmc::spawn(t);
      EXPECT_EQ(x, 2);
    }
  }());
}

// Doesn't compile - as expected. fork() types cannot be moved
// static inline tmc::task<void> spawn_many_compose_fork() {
//   {
//     std::array<tmc::aw_spawn_fork<tmc::task<int>>, 2> ts{
//       tmc::spawn(work(0)).fork(), tmc::spawn(work(1)).fork()
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
//     std::array<tmc::aw_spawn_fork<tmc::task<void>>, 2> ts{
//       tmc::spawn(set(void_results[0])).fork(),
//       tmc::spawn(set(void_results[1])).fork()
//     };
//     co_await tmc::spawn_many<2>(ts.data());

//     auto sum = void_results[0] + void_results[1];

//     EXPECT_EQ(sum, (1 << 2) - 1);
//   }
// }

// TEST_F(CATEGORY, spawn_many_compose_fork) {
//   test_async_main(ex(), []() -> tmc::task<void> {
//     co_await spawn_many_compose_fork();
//   }());
// }

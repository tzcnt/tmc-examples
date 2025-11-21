#include "test_common.hpp"
#include "tmc/all_headers.hpp"
#include "tmc/detail/bitmap_object_pool.hpp"

#include <atomic>
#include <gtest/gtest.h>
#include <ranges>
#include <vector>

using namespace tmc::detail;

#define CATEGORY test_bitmap_object_pool

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(16).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

// Pool customization to pass through external pointer variable
// to each destructor_counter constructor
class DestructorCounterPool
    : public BitmapObjectPoolImpl<destructor_counter, DestructorCounterPool> {
  friend class BitmapObjectPoolImpl<destructor_counter, DestructorCounterPool>;
  void initialize(void* location) {
    ::new (location) destructor_counter(destroyed_count);
  }

public:
  std::atomic<size_t>* destroyed_count;
  DestructorCounterPool(std::atomic<size_t>& Count)
      : BitmapObjectPoolImpl<destructor_counter, DestructorCounterPool>(),
        destroyed_count{&Count} {}
};

template <size_t Count> void destructor_count_test(tmc::ex_cpu& ex) {
  test_async_main(ex, []() -> tmc::task<void> {
    std::atomic<size_t> destroyed_count = 0;
    {
      DestructorCounterPool pool{destroyed_count};
      tmc::barrier bar(Count);

      auto tasks =
        std::ranges::views::iota(static_cast<size_t>(0), Count) |
        std::ranges::views::transform([&](size_t) -> tmc::task<void> {
          return [](
                   DestructorCounterPool& Pool, tmc::barrier& Bar
                 ) -> tmc::task<void> {
            auto obj = Pool.acquire_scoped();
            // Use this barrier to force each task to acquire a newly
            // created pool object, before releasing them all
            co_await Bar;
            obj.release();
          }(pool, bar);
        });
      co_await tmc::spawn_many(tasks);
    }
    EXPECT_EQ(destroyed_count.load(), Count);
  }());
}

TEST_F(CATEGORY, destructor_count_0) { destructor_count_test<0>(ex()); }
TEST_F(CATEGORY, destructor_count_1) { destructor_count_test<1>(ex()); }
TEST_F(CATEGORY, destructor_count_63) { destructor_count_test<63>(ex()); }
TEST_F(CATEGORY, destructor_count_64) { destructor_count_test<64>(ex()); }
TEST_F(CATEGORY, destructor_count_127) { destructor_count_test<127>(ex()); }
TEST_F(CATEGORY, destructor_count_128) { destructor_count_test<128>(ex()); }
TEST_F(CATEGORY, destructor_count_9999) { destructor_count_test<9999>(ex()); }

template <size_t Count> void destructor_count_test_wfpg(tmc::ex_cpu& ex) {
  test_async_main(ex, []() -> tmc::task<void> {
    std::atomic<size_t> destroyed_count = 0;
    {
      DestructorCounterPool pool{destroyed_count};
      tmc::barrier bar(Count);

      auto tasks =
        std::ranges::views::iota(static_cast<size_t>(0), Count) |
        std::ranges::views::transform([&](size_t i) -> tmc::task<void> {
          return [](
                   DestructorCounterPool& Pool, tmc::barrier& Bar, size_t Idx
                 ) -> tmc::task<void> {
            if (Idx % 2 == 0) {
              auto obj = Pool.acquire_scoped_wfpg<0>();
              // Use this barrier to force each task to acquire a newly
              // created pool object, before releasing them all
              co_await Bar;
              obj.release();
            } else {
              auto obj = Pool.acquire_scoped_wfpg<1>();
              // Use this barrier to force each task to acquire a newly
              // created pool object, before releasing them all
              co_await Bar;
              obj.release();
            }
          }(pool, bar, i);
        });
      co_await tmc::spawn_many(tasks);
    }
    EXPECT_EQ(destroyed_count.load(), Count);
  }());
}

TEST_F(CATEGORY, destructor_count_wfpg_0) {
  destructor_count_test_wfpg<0>(ex());
}
TEST_F(CATEGORY, destructor_count_wfpg_1) {
  destructor_count_test_wfpg<1>(ex());
}
TEST_F(CATEGORY, destructor_count_wfpg_63) {
  destructor_count_test_wfpg<63>(ex());
}
TEST_F(CATEGORY, destructor_count_wfpg_64) {
  destructor_count_test_wfpg<64>(ex());
}
TEST_F(CATEGORY, destructor_count_wfpg_127) {
  destructor_count_test<127>(ex());
}
TEST_F(CATEGORY, destructor_count_wfpg_128) {
  destructor_count_test<128>(ex());
}
TEST_F(CATEGORY, destructor_count_wfpg_9999) {
  destructor_count_test<9999>(ex());
}

template <size_t Count> void vector_test(tmc::ex_cpu& ex) {
  test_async_main(ex, []() -> tmc::task<void> {
    BitmapObjectPool<std::vector<size_t>> pool;

    auto tasks =
      std::ranges::views::iota(static_cast<size_t>(0), Count) |
      std::ranges::views::transform([&](size_t i) -> tmc::task<void> {
        return [](
                 BitmapObjectPool<std::vector<size_t>>& Pool, size_t idx
               ) -> tmc::task<void> {
          auto obj = Pool.acquire_scoped();
          auto& vec = obj.value;
          vec.push_back(idx);
          obj.release();
          co_return;
        }(pool, i);
      });
    co_await tmc::spawn_many(tasks);

    std::vector<size_t> results;
    results.reserve(Count);
    size_t vecCount = 0;
    pool.for_each_available(
      [&results, &vecCount](std::vector<size_t>& poolVec) -> void {
        results.insert(
          results.end(), std::make_move_iterator(poolVec.begin()),
          std::make_move_iterator(poolVec.end())
        );
        ++vecCount;
      }
    );

    // Each value should be present although they may be in different pools
    std::sort(results.begin(), results.end());
    for (size_t i = 0; i < Count; ++i) {
      EXPECT_EQ(i, results[i]);
    }
  }());
}

TEST_F(CATEGORY, vector_0) { vector_test<0>(ex()); }
TEST_F(CATEGORY, vector_1) { vector_test<1>(ex()); }
TEST_F(CATEGORY, vector_63) { vector_test<63>(ex()); }
TEST_F(CATEGORY, vector_64) { vector_test<64>(ex()); }
TEST_F(CATEGORY, vector_127) { vector_test<127>(ex()); }
TEST_F(CATEGORY, vector_128) { vector_test<128>(ex()); }
TEST_F(CATEGORY, vector_9999) { vector_test<9999>(ex()); }

TEST_F(CATEGORY, vector_count) {
  test_async_main(ex(), []() -> tmc::task<void> {
    BitmapObjectPool<std::vector<size_t>> pool;

    size_t Count = 9999;
    tmc::barrier bar(Count + 1);
    auto tasks =
      std::ranges::views::iota(static_cast<size_t>(0), Count) |
      std::ranges::views::transform([&](size_t i) -> tmc::task<void> {
        return [](
                 BitmapObjectPool<std::vector<size_t>>& Pool, size_t idx,
                 tmc::barrier& Bar
               ) -> tmc::task<void> {
          auto obj = Pool.acquire_scoped();
          auto& vec = obj.value;
          vec.push_back(idx);
          // Signal we are done. Barrier will reset automatically.
          co_await Bar;
          // Wait for the outer task to finish processing (to test
          // for_each_in_use and for_each_unsafe)
          co_await Bar;
          obj.release();
          co_return;
        }(pool, i, bar);
      });
    auto ts = tmc::spawn_many(tasks).fork();
    co_await bar;
    // Test for_each_unsafe on checked out elements
    {
      std::vector<size_t> results;
      results.reserve(Count);
      size_t vecCount = 0;
      pool.for_each_in_use(
        []() -> bool { return true; },
        [&results, &vecCount](std::vector<size_t>& poolVec) -> void {
          EXPECT_EQ(poolVec.size(), 1);
          results.push_back(poolVec[0]);
          ++vecCount;
        }
      );

      // Each value should be present in a different pool object
      EXPECT_EQ(vecCount, 9999);

      std::sort(results.begin(), results.end());
      for (size_t i = 0; i < Count; ++i) {
        EXPECT_EQ(i, results[i]);
      }
    }
    // Test for_each_in_use on checked out elements
    {
      std::vector<size_t> results;
      results.reserve(Count);
      size_t vecCount = 0;
      pool.for_each_in_use(
        []() -> bool { return true; },
        [&results, &vecCount](std::vector<size_t>& poolVec) -> void {
          EXPECT_EQ(poolVec.size(), 1);
          results.push_back(poolVec[0]);
          ++vecCount;
        }
      );

      // Each value should be present in a different pool object
      EXPECT_EQ(vecCount, 9999);

      std::sort(results.begin(), results.end());
      for (size_t i = 0; i < Count; ++i) {
        EXPECT_EQ(i, results[i]);
      }
    }
    // Allow all of the tasks to complete and release their elements
    co_await bar;
    co_await std::move(ts);
    // Test for_each_available (all elements should be available)
    {
      std::vector<size_t> results;
      results.reserve(Count);
      size_t vecCount = 0;
      pool.for_each_available(
        [&results, &vecCount](std::vector<size_t>& poolVec) -> void {
          EXPECT_EQ(poolVec.size(), 1);
          results.push_back(poolVec[0]);
          ++vecCount;
        }
      );

      // Each value should be present in a different pool object
      EXPECT_EQ(vecCount, 9999);

      std::sort(results.begin(), results.end());
      for (size_t i = 0; i < Count; ++i) {
        EXPECT_EQ(i, results[i]);
      }
    }
    {
      std::vector<size_t> results;
      results.reserve(Count);
      size_t vecCount = 0;
      pool.for_each_available(
        [&results, &vecCount](std::vector<size_t>& poolVec) -> void {
          EXPECT_EQ(poolVec.size(), 1);
          results.push_back(poolVec[0]);
          ++vecCount;
        }
      );

      // Each value should be present in a different pool object
      EXPECT_EQ(vecCount, 9999);

      std::sort(results.begin(), results.end());
      for (size_t i = 0; i < Count; ++i) {
        EXPECT_EQ(i, results[i]);
      }
    }
  }());
}

TEST_F(CATEGORY, initialize_with_params) {
  BitmapObjectPool<std::vector<size_t>> pool;
  {
    auto obj = pool.acquire_scoped(5u);
    EXPECT_EQ(obj.value.size(), 5);
    obj.release();
  }
  {
    auto obj = pool.acquire_scoped();
    EXPECT_EQ(obj.value.size(), 5);
    obj.release();
  }
}

struct move_only_type {
  int value;

  move_only_type(int input) : value(input) {}
  move_only_type& operator=(move_only_type&&) = default;
  move_only_type(move_only_type&&) = default;
  ~move_only_type() = default;

  // No default or copy constructor
  move_only_type() = delete;
  move_only_type(const move_only_type&) = delete;
  move_only_type& operator=(const move_only_type&) = delete;
};

// Verify that the pool can hold an object that has no default constructor
TEST_F(CATEGORY, no_default_constructor) {
  BitmapObjectPool<move_only_type> pool;
  {
    auto obj = pool.acquire_scoped(5);
    EXPECT_EQ(obj.value.value, 5);
    obj.release();
  }
  {
    auto obj = pool.acquire_scoped(999);
    // we should get the same object again - not 999
    EXPECT_EQ(obj.value.value, 5);
    obj.release();
  }
}

// Verify that perfect forwarding into the held object constructor works
TEST_F(CATEGORY, move_construct) {
  BitmapObjectPool<move_only_type> pool;
  {
    auto obj = pool.acquire_scoped(move_only_type{5});
    EXPECT_EQ(obj.value.value, 5);
    obj.release();
  }
  {
    auto obj = pool.acquire_scoped(move_only_type{999});
    // we should get the same object again - not 999
    EXPECT_EQ(obj.value.value, 5);
    obj.release();
  }
}
#undef CATEGORY

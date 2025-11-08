#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>

template <typename T> class BitmapObjectPool {
  static constexpr uint64_t ONE_BIT = static_cast<uint64_t>(1);
  std::atomic<uint64_t> available_bits;
  std::array<T, 64> objects;

public:
  // This version eagerly default-constructs 64 of the pooled object.
  // Thus it starts with 64 available objects (all bits are 1).
  BitmapObjectPool() : available_bits{static_cast<uint64_t>(-1)} {}

  bool try_acquire(uint64_t& idx) {
    auto bits = available_bits.load(std::memory_order_relaxed);
    while (true) {
      if (bits == 0) {
        return false;
      }
      idx = static_cast<uint64_t>(std::countr_zero(bits));
      auto bit = ONE_BIT << idx;
      // Clear this bit to take ownership of the object
      bits = available_bits.fetch_and(~bit);
      if ((bits & bit) != 0) {
        return true;
      }
    }
  }

  uint64_t acquire() {
    size_t idx;
    [[maybe_unused]] bool ok = try_acquire(idx);
    assert(ok && "All pool objects are in use!");
    return idx;
  }

  void release(uint64_t idx) {
    auto bit = ONE_BIT << idx;
    // Set this bit to release ownership of the object
    [[maybe_unused]] auto old = available_bits.fetch_or(bit);
    assert((old & bit) == 0 && "Released object you didn't own!");
  }

  // Pass the index that you got from acquire()
  T& ref(uint64_t idx) { return objects[idx]; }

  class ScopedPoolObject {
    friend BitmapObjectPool;

  public:
    T& value;

  private:
    BitmapObjectPool& pool;
    uint64_t idx;
    ScopedPoolObject(BitmapObjectPool& Pool, uint64_t Idx)
        : value{Pool.ref(Idx)}, pool{Pool}, idx{Idx} {}

  public:
    ~ScopedPoolObject() { pool.release(idx); }
  };

  ScopedPoolObject acquire_scoped() {
    return ScopedPoolObject{*this, acquire()};
  }
};

// DEMO / TEST
// Using TMC to test this because it's an easy way to spin up multiple threads,
// but not required for the above code to work.
#define TMC_IMPL
#include "tmc/all_headers.hpp"
#include <ranges>
#include <string>
#include <unordered_map>

void print_from_pool(
  BitmapObjectPool<std::unordered_map<int, std::string>>& pool, int i
) {
  if (i % 2 == 0) {
    // Demonstrate the use of manual acquire/release
    auto idx = pool.acquire();
    std::unordered_map<int, std::string>& map = pool.ref(idx);

    const auto iter = map.find(i);
    if (iter != map.cend()) {
      auto s = iter->second;
      // std::printf("%s ", s.c_str());
    } else {
      auto [it, inserted] = map.emplace(i, std::to_string(i));
      assert(inserted);
      std::printf("%s ", it->second.c_str());
    }

    pool.release(idx);
  } else {
    // Demonstrate the use of the scoped object
    auto obj = pool.acquire_scoped();
    std::unordered_map<int, std::string>& map = obj.value;

    const auto iter = map.find(i);
    if (iter != map.cend()) {
      auto s = iter->second;
      // std::printf("%s ", s.c_str());
    } else {
      auto [it, inserted] = map.emplace(i, std::to_string(i));
      assert(inserted);
      std::printf("%s ", it->second.c_str());
    }
  }
}

tmc::task<void>
pool_user(BitmapObjectPool<std::unordered_map<int, std::string>>& pool) {
  for (size_t i = 0; i < 10; ++i) {
    print_from_pool(pool, i);
  }
  co_return;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  tmc::cpu_executor().set_thread_count(4);
  return tmc::async_main([]() -> tmc::task<int> {
    BitmapObjectPool<std::unordered_map<int, std::string>> stringPool;

    auto tasks = std::ranges::views::iota(0, 10000) |
                 std::ranges::views::transform([&](int i) -> tmc::task<void> {
                   return pool_user(stringPool);
                 });
    co_await tmc::spawn_many(tasks);
    co_return 0;
  }());
}

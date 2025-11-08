#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>

template <typename T, typename Derived> class BitmapObjectPool {
  static constexpr uint64_t ONE_BIT = static_cast<uint64_t>(1);
  std::atomic<uint64_t> available_bits;
  std::atomic<uint64_t> count;
  std::array<T, 64> objects;

public:
  // This version lazily initializes objects as needed.
  // Thus it starts with 0 available objects (all bits are 0).
  BitmapObjectPool() : available_bits{0}, count{0} {}

protected:
  bool try_init(uint64_t& idx) {
    idx = count.load(std::memory_order_relaxed);
    while (true) {
      if (idx >= 64) {
        return false;
      }
      if (count.compare_exchange_strong(idx, idx + 1)) {
        // Use CRTP to delegate initialization to derived class
        static_cast<Derived*>(this)->init(objects[idx]);
        return true;
      }
    }
  }

public:
  // Try to acquire each currently available element of the list one-by-one and
  // run func() on it.
  template <typename Fn> void for_each_available(Fn func) {
    auto max = count.load(std::memory_order_relaxed);
    for (uint64_t i = 0; i < max; ++i) {
      auto bit = ONE_BIT << i;
      // Try to clear this bit to take ownership of the object.
      // If it was already clear, nothing happens.
      auto bits = available_bits.fetch_and(~bit);
      if ((bits & bit) != 0) {
        // We now own this object. Run the caller's functor on it.
        func(objects[i]);
        // Now release the object
        available_bits.fetch_or(bit);
      }
    }
  }

  bool try_acquire(uint64_t& idx) {
    auto bits = available_bits.load(std::memory_order_relaxed);
    while (true) {
      if (bits == 0) {
        return try_init(idx);
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

// CRTP customization to reserve space for any container type
template <typename C>
class ContainerPool : public BitmapObjectPool<C, ContainerPool<C>> {
  friend class BitmapObjectPool<C, ContainerPool<C>>;

  // Implement init() for the container type
  void init(C& newContainer) { newContainer.reserve(500); }

  // This replaces the need for the map_vector functionality
  // instead we just process any free maps one-by-one in place
  void clean() {
    BitmapObjectPool<C, ContainerPool<C>>::for_each_available([](C& map) {
      // if (absl::erase_if(map, [](const auto& pair) {
      //       return pair.second->dwReference == 0;
      //     }) > 0)
      //   map.rehash(0);
    });
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
  ContainerPool<std::unordered_map<int, std::string>>& pool, int i
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
pool_user(ContainerPool<std::unordered_map<int, std::string>>& pool) {
  for (size_t i = 0; i < 10; ++i) {
    print_from_pool(pool, i);
  }
  co_return;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  tmc::cpu_executor().set_thread_count(4);
  return tmc::async_main([]() -> tmc::task<int> {
    ContainerPool<std::unordered_map<int, std::string>> stringPool;

    auto tasks = std::ranges::views::iota(0, 10000) |
                 std::ranges::views::transform([&](int i) -> tmc::task<void> {
                   return pool_user(stringPool);
                 });
    co_await tmc::spawn_many(tasks);
    co_return 0;
  }());
}

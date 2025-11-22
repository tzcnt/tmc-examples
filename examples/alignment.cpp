// This example is a reproducer for the below bugs:

// Clang <16 does not obey alignment when creating coroutine frames
// https://github.com/llvm/llvm-project/issues/56671
// This test seems to reproduce the failure in Debug mode at least
// In Clang versions 16 or higher you must supply `-fcoro-aligned-allocation`
// to fix it.

// Bug also exists in GCC 13
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104177
// This test repros in Debug or Release

#define TMC_IMPL

#include "tmc/aw_yield.hpp"
#include "tmc/detail/compat.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <cstdio>
#include <ranges>
#include <vector>

constexpr size_t ALIGNMENT = 64;
struct unaligned_struct {
  char value;
};
struct alignas(ALIGNMENT) aligned_struct {
  int value;
  int value2;
  TMC_DISABLE_WARNING_PADDED_BEGIN
};
TMC_DISABLE_WARNING_PADDED_END

static void check_alignment(void* ptr) {
  auto low_bits = reinterpret_cast<size_t>(ptr) % ALIGNMENT;
  if (low_bits != 0) {
    std::printf(
      "FAIL: Expected align %zu but got align %zu\n", ALIGNMENT, low_bits
    );
    std::fflush(stdout);
  }
}
static tmc::task<void>
run_one(size_t i, unaligned_struct* ur, aligned_struct* ar) {
  static_assert(alignof(aligned_struct) == ALIGNMENT);
  static_assert(sizeof(void*) == sizeof(size_t));
  unaligned_struct u;
  aligned_struct a;
  u.value = static_cast<char>(i & 0xFF);
  a.value = static_cast<int>(i + 1);
  check_alignment(&a);
  co_await tmc::yield();
  a.value2 = static_cast<int>(i + 2);
  check_alignment(&a);
  *ur = u;
  *ar = a;
}
template <size_t Count> tmc::task<void> run() {
  std::vector<unaligned_struct> r1;
  std::vector<aligned_struct> r2;
  r1.resize(Count);
  r2.resize(Count);
  auto tasks =
    std::ranges::views::iota(static_cast<size_t>(0), Count) |
    std::ranges::views::transform([&](size_t idx) -> tmc::task<void> {
      return run_one(idx, &r1[idx], &r2[idx]);
    });
  co_await tmc::spawn_many(tasks);
  for (size_t i = 0; i < Count; ++i) {
    if (r2[i].value == 0) {
      std::printf("fail");
    }
  }
}
int main() {
  tmc::async_main([]() -> tmc::task<int> {
    co_await run<32000>();
    co_return 0;
  }());
}

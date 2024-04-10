// This example is a reproducer for the below bugs:

// Clang <16 does not obey alignment when creating coroutine frames
// https://github.com/llvm/llvm-project/issues/56671
// This test seems to reproduce the failure in Debug mode at least
// In Clang 16 you must supply `-fcoro-aligned-allocation` to fix it.

// Bug also exists in GCC 13
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104177
// This test repros in Debug or Release

#define TMC_IMPL

#include "tmc/aw_yield.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_task_many.hpp"
#include "tmc/utils.hpp"

#include <cinttypes>
#include <cstdio>

using namespace tmc;

constexpr size_t ALIGNMENT = 64;
struct unaligned_struct {
  char value;
};
struct alignas(ALIGNMENT) aligned_struct {
  int value;
  int value2;
};

static void check_alignment(void* ptr) {
  auto low_bits = reinterpret_cast<uint64_t>(ptr) % ALIGNMENT;
  if (low_bits != 0) {
    std::printf(
      "FAIL: Expected align %" PRIu64 " but got align %" PRIu64 "\n", ALIGNMENT,
      low_bits
    );
    std::fflush(stdout);
  }
}
static task<void> run_one(int i, unaligned_struct* ur, aligned_struct* ar) {
  static_assert(alignof(aligned_struct) == ALIGNMENT);
  static_assert(sizeof(void*) == sizeof(uint64_t));
  unaligned_struct u;
  aligned_struct a;
  u.value = i & 0xFF;
  a.value = i + 1;
  check_alignment(&a);
  co_await yield();
  a.value2 = i + 2;
  check_alignment(&a);
  *ur = u;
  *ar = a;
}
template <size_t Count> tmc::task<void> run() {
  std::vector<unaligned_struct> r1;
  std::vector<aligned_struct> r2;
  r1.resize(Count);
  r2.resize(Count);
  co_await spawn_many(
    iter_adapter(
      0, [&](int idx) -> task<void> { return run_one(idx, &r1[idx], &r2[idx]); }
    ),
    Count
  );
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

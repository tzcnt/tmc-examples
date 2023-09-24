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
#include <iostream>
using namespace tmc;

constexpr size_t ALIGNMENT = 64;
struct UnalignedStruct {
  char value;
};
struct alignas(ALIGNMENT) AlignedStruct {
  int value;
  int value2;
};
void check_alignment(void *ptr) {
  auto low_bits = reinterpret_cast<uint64_t>(ptr) % ALIGNMENT;
  if (low_bits != 0) {
    std::printf("FAIL: Expected align %ld but got align %ld\n", ALIGNMENT,
                low_bits);
    std::cout.flush();
  }
}
task<void> run_one(size_t i, UnalignedStruct *ur, AlignedStruct *ar) {
  static_assert(alignof(AlignedStruct) == ALIGNMENT);
  static_assert(sizeof(void *) == sizeof(uint64_t));
  UnalignedStruct u;
  AlignedStruct a;
  u.value = i & 0xFF;
  a.value = i + 1;
  check_alignment(&a);
  co_await yield();
  a.value2 = i + 2;
  check_alignment(&a);
  *ur = u;
  *ar = a;
}
template <size_t count> tmc::task<void> run() {
  std::vector<UnalignedStruct> r1;
  std::vector<AlignedStruct> r2;
  r1.resize(count);
  r2.resize(count);
  co_await spawn_many(iter_adapter(0,
                                   [&](size_t idx) -> task<void> {
                                     return run_one(idx, &r1[idx], &r2[idx]);
                                   }),
                      count);
  for (size_t i = 0; i < count; ++i) {
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

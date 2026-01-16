// A demonstration of the capabilities and performance of `ex_braid`
// It is both a serializing executor, and an async mutex
// Similar to asio::strand

#define TMC_IMPL

#include "tmc/aw_resume_on.hpp"
#include "tmc/ex_braid.hpp"
#include "tmc/ex_cpu.hpp"
#include "tmc/spawn_func.hpp"
#include "tmc/spawn_group.hpp"
#include "tmc/spawn_many.hpp"
#include "tmc/task.hpp"

#include <cstdio>
#include <ranges>

static inline constexpr size_t TASK_COUNT = 10000;

// Child tasks that run_on(braid) will execute serially
static tmc::task<void> child_tasks_run_on_braid() {
  tmc::ex_braid br;
  size_t value = 0;
  auto sg = tmc::spawn_group();
  for (size_t i = 0; i < TASK_COUNT; ++i) {
    sg.add([](size_t& v) -> tmc::task<void> {
      v++;
      co_return;
    }(value));
  }
  co_await std::move(sg).run_on(br);
  // Only the child tasks run on the braid.
  // Afterward, this task resumes on its original executor (not the braid).
  if (value != TASK_COUNT) {
    std::printf("FAIL: expected %zu but got %zu\n", TASK_COUNT, value);
  }
}

// Non-coroutine functions can also be serialized this way
static tmc::task<void> child_funcs_run_on_braid() {
  tmc::ex_braid br;
  size_t value = 0;
  auto funcs =
    std::ranges::views::iota(0, static_cast<int>(TASK_COUNT)) |
    std::ranges::views::transform([&](int) { return [&]() { value++; }; });
  co_await tmc::spawn_func_many(funcs).run_on(br);
  // Only the child funcs run on the braid.
  // Afterward, this task resumes on its original executor (not the braid).
  if (value != TASK_COUNT) {
    std::printf("FAIL: expected %zu but got %zu\n", TASK_COUNT, value);
  }
}

// Use the braid as a lock at the end of a task
static tmc::task<void> braid_lock() {
  tmc::ex_braid br;
  size_t value = 0;
  auto sg = tmc::spawn_group();
  for (size_t i = 0; i < TASK_COUNT; ++i) {
    sg.add([](tmc::ex_braid& braid, size_t& v) -> tmc::task<void> {
      // ... do some work in parallel ...

      // Enter the braid for serial execution portion
      co_await tmc::enter(braid);
      v++;
      // not necessary to exit the braid scope, since the task has ended
      co_return;
    }(br, value));
  }
  co_await std::move(sg).run_on(br);
  if (value != TASK_COUNT) {
    std::printf("FAIL: expected %zu but got %zu\n", TASK_COUNT, value);
  }
}

// Use the braid as a lock in the middle of a task
static tmc::task<void> braid_lock_middle() {
  tmc::ex_braid br;
  size_t value = 0;
  auto sg = tmc::spawn_group();
  for (size_t i = 0; i < TASK_COUNT; ++i) {
    sg.add([](tmc::ex_braid& braid, size_t& v) -> tmc::task<void> {
      // ... do some work in parallel ...

      // Enter the braid for serial execution portion only
      auto braidScope = co_await tmc::enter(braid);
      v++;
      co_await braidScope.exit();

      // ... more parallel work ...

      co_return;
    }(br, value));
  }
  co_await std::move(sg).run_on(br);
  if (value != TASK_COUNT) {
    std::printf("FAIL: expected %zu but got %zu\n", TASK_COUNT, value);
  }
}

// Use the braid as a lock in the middle of a task
// Same as prior example but uses resume_on() instead of enter()/exit()
// This means we need to know which executor we are exiting to
static tmc::task<void> braid_lock_middle_resume_on() {
  tmc::ex_braid br;
  size_t value = 0;
  auto sg = tmc::spawn_group();
  for (size_t i = 0; i < TASK_COUNT; ++i) {
    sg.add([](tmc::ex_braid& braid, size_t& v) -> tmc::task<void> {
      // ... do some work in parallel ...

      // Enter the braid for serial execution portion only
      co_await tmc::resume_on(braid);
      v++;
      co_await tmc::resume_on(tmc::cpu_executor());

      // ... more parallel work ...

      co_return;
    }(br, value));
  }
  co_await std::move(sg).run_on(br);
  if (value != TASK_COUNT) {
    std::printf("FAIL: expected %zu but got %zu\n", TASK_COUNT, value);
  }
}

// Lock in the middle of the task using a child function
static tmc::task<void> braid_lock_middle_child() {
  tmc::ex_braid br;
  size_t value = 0;
  auto sg = tmc::spawn_group();
  for (size_t i = 0; i < TASK_COUNT; ++i) {
    sg.add([](tmc::ex_braid& braid, size_t& v) -> tmc::task<void> {
      // ... do some work in parallel ...

      // Run only the increment function on the braid
      co_await tmc::spawn_func([&]() { v++; }).run_on(braid);

      // ... more parallel work ...

      co_return;
    }(br, value));
  }
  co_await std::move(sg).run_on(br);
  if (value != TASK_COUNT) {
    std::printf("FAIL: expected %zu but got %zu\n", TASK_COUNT, value);
  }
}

static tmc::task<int> async_main() {
  co_await child_tasks_run_on_braid();
  co_await child_funcs_run_on_braid();
  co_await braid_lock();
  co_await braid_lock_middle();
  co_await braid_lock_middle_resume_on();
  co_await braid_lock_middle_child();
  co_return 0;
}
int main() { return tmc::async_main(async_main()); }

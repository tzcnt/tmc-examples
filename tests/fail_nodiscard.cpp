TEST(CATEGORY, task_func_post) {
  // Accidentally submitted a task-returning func without calling it
  EXPECT_DEATH(
    {
      tmc::ex_cpu ex;
      ex.set_thread_count(1).init();
      // get() makes this nodiscard
      tmc::post_waitable(ex, empty_task, 0).get();
    },
    "!handle"
  );
}

tmc::post_bulk_waitable(
  tmc::cpu_executor(),
  tmc::iter_adapter(0, [](int i) -> tmc::task<int> { co_return 1; }), 10, 0
)
  .get();
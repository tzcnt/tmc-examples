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
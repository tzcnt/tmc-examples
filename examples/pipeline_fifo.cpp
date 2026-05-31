// A parallel actor-based data pipeline with:
// - Processing functions can be regular functions or coroutines
// - Configurable number of stages / input / output types
// - Configurable parallelism per stage
// - Automatic backpressure based on the current stage parallelism
// - Parallel processing of inputs with FIFO serialization of outputs

#include "tmc/ex_cpu.hpp"
#include "tmc/fork_group.hpp"
#include "tmc/task.hpp"

// This is just the user application.
// The generic FIFO pipeline implementation is in the header
#include "pipeline_fifo.hpp"

#include <chrono>
#include <cstdio>
#include <string>

static inline constexpr int NELEMS = 1'000'000;

static std::string formatElementsPerSec(size_t durMs) {
  size_t elementsPerSec = static_cast<size_t>(
    static_cast<double>(NELEMS) * 1'000.0 / (static_cast<double>(durMs))
  );
  auto s = std::to_string(elementsPerSec);
  int i = static_cast<int>(s.length()) - 3;
  while (i > 0) {
    s.insert(static_cast<size_t>(i), ",");
    i -= 3;
  }
  return s;
}

// Example processing steps - these can be coroutines or regular functions
static float plus_half(int i) { return static_cast<float>(i) + 0.5f; }
static tmc::task<double> times_two(float i) {
  co_return static_cast<double>(2.0f * i);
}
static int minus_one(double i) { return static_cast<int>(i) - 1; }
static bool as_bool(int i) { return i > 2; }

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  return tmc::async_main([]() -> tmc::task<int> {
    std::printf("testing %d items through 5-stage pipeline...\n", NELEMS);
    auto constructStart = std::chrono::high_resolution_clock::now();

    // Track all pipeline stage workers here so we can cleanly join at the end
    auto fg = tmc::fork_group();

    // Each stage owns its own output queue, sized to that stage's parallelism
    // parameter. The bounded output queue provides backpressure and gates the
    // stage's own in-flight parallelism. The start stage additionally owns
    // its input queue (the queue the driver pushes to), exposed as `.in`.
    auto first = start_pipeline<int>(fg, plus_half, 10);
    auto second = pipeline_transform(fg, first, times_two, 10);
    auto third = pipeline_transform(fg, second, minus_one, 10);
    auto fourth = pipeline_transform(fg, third, as_bool, 10);

    size_t sum = 0;
    size_t count = 0;

    // This final stage has been configured to serialize the results by setting
    // parallelism = 1. Results will appear in the same order as the original
    // input. Another option would be to consume the results directly from the
    // prior stage's get_queue().
    [[maybe_unused]] auto fifth = end_pipeline(
      fg, fourth,
      [&sum, &count](bool i) {
        sum += static_cast<size_t>(i);
        count += 1;
      },
      1
    );

    auto processStart = std::chrono::high_resolution_clock::now();

    {
      // Run the initial producer task inline. Optionally, this could also be
      // added to the fg.
      for (int i = 0; i < NELEMS; ++i) {
        // The bounded queue's push() suspends when the queue is full,
        // providing backpressure to the producer.
        co_await first.input_queue->push(i);
      }
      // Close the input queue. The first stage worker will drain remaining
      // items and then close its output queue, propagating closure through
      // the pipeline. Awaiting the fork_group below ensures full drain.
      first.input_queue->close();
    }

    co_await std::move(fg);

    auto processEnd = std::chrono::high_resolution_clock::now();

    std::printf("element sum: %zu\n", sum);     // should be 999998
    std::printf("element count: %zu\n", count); // should be 1000000

    size_t constructTime =
      static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            processStart - constructStart
      )
                            .count());

    size_t processTime =
      static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            processEnd - processStart
      )
                            .count());

    std::printf("construct time: %f ms\n", static_cast<double>(constructTime));
    std::printf("process time: %f ms\n", static_cast<double>(processTime));
    std::printf("%s elements/sec\n", formatElementsPerSec(processTime).c_str());
    co_return 0;
  }());
}

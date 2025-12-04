// A parallel actor-based data pipeline with:
// - Processing functions can be regular functions or coroutines
// - Configurable number of stages / input / output types
// - Configurable number of workers per stage
// - Automatic backpressure based on the number of workers in the current stage

// This pipeline will implement FIFO processing for any connection between 2
// stages that both have 1 worker. If any stage has more than 1 worker, then
// processing will only be "roughly FIFO" due to parallel workers possibly
// completing out of order.

#define TMC_IMPL

#include "tmc/all_headers.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <tuple>
#include <type_traits>

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

template <typename Input, typename Output, typename Func>
tmc::task<void> worker_func(
  tmc::chan_tok<Input> inChan, tmc::semaphore* inSem,
  tmc::chan_tok<Output> outChan, tmc::semaphore* outSem, Func func
) {
  while (true) {
    // TMC semaphores are unfair / LIFO.
    // In order to implement "roughly FIFO" processing, ensure we have the
    // output semaphore before we get any data from the input channel.
    // To account for this extra time with the semaphore held, the size of the
    // semaphore is doubled to 2*nextWorkerCount.
    if (outSem != nullptr) {
      co_await *outSem;
    }
    auto input = co_await inChan.pull();
    if (!input.has_value()) {
      break;
    }
    if (inSem != nullptr) {
      inSem->release();
    }
    co_await outChan.push(func(input.value()));
  }
}

template <typename Input, typename Output, typename Func>
tmc::task<void> worker_coro(
  tmc::chan_tok<Input> inChan, tmc::semaphore* inSem,
  tmc::chan_tok<Output> outChan, tmc::semaphore* outSem, Func func
) {
  while (true) {
    // TMC semaphores are unfair / LIFO.
    // In order to implement "roughly FIFO" processing, ensure we have the
    // output semaphore before we get any data from the input channel.
    // To account for this extra time with the semaphore held, the size of the
    // semaphore is doubled to 2*nextWorkerCount.
    if (outSem != nullptr) {
      co_await *outSem;
    }
    auto input = co_await inChan.pull();
    if (!input.has_value()) {
      break;
    }
    if (inSem != nullptr) {
      inSem->release();
    }
    co_await outChan.push(co_await func(input.value()));
  }
}

template <typename Input, typename Output, typename Func, bool End>
struct pipeline_stage_func {
  using output_t = Output;

  tmc::chan_tok<Output> outChan_;
  tmc::semaphore outSem_;

  pipeline_stage_func(
    tmc::chan_tok<Input> inChan, tmc::semaphore* inSem, Func func,
    size_t workerCount
  )
      : outChan_(tmc::make_channel<Output>()),
        outSem_(tmc::semaphore(tmc::semaphore::half_word(2 * workerCount))) {
    tmc::semaphore* outSem = &outSem_;
    if constexpr (End) {
      outSem = nullptr;
    }

    auto sg = tmc::spawn_group();
    for (size_t i = 0; i < workerCount; ++i) {
      sg.add(worker_func(inChan, inSem, outChan_, outSem, func));
    }

    // Create a task that automatically closes the output channel after all of
    // the workers finish.
    tmc::spawn([](auto SG, auto Chan) -> tmc::task<void> {
      co_await std::move(SG);
      co_await Chan.drain();
    }(std::move(sg), outChan_))
      .detach();
  }

  tmc::chan_tok<Output> get_channel() { return outChan_; }
};

template <typename Input, typename Func>
auto start_pipeline_func(
  tmc::chan_tok<Input> from, Func transformFunc, size_t workerCount = 1
) {
  using Output = std::invoke_result_t<Func, Input>;
  return pipeline_stage_func<Input, Output, Func, false>{
    from, nullptr, transformFunc, workerCount
  };
}

template <typename PriorStage, typename Func>
auto pipeline_transform_func(
  PriorStage& from, Func transformFunc, size_t workerCount = 1
) {
  using Input = PriorStage::output_t;
  using Output = std::invoke_result_t<Func, Input>;
  return pipeline_stage_func<Input, Output, Func, false>{
    from.outChan_, &from.outSem_, transformFunc, workerCount
  };
}

template <typename PriorStage, typename Func>
auto end_pipeline_func(
  PriorStage& from, Func transformFunc, size_t workerCount = 1
) {
  using Input = PriorStage::output_t;
  using Output = std::invoke_result_t<Func, Input>;
  return pipeline_stage_func<Input, Output, Func, true>{
    from.outChan_, &from.outSem_, transformFunc, workerCount
  };
}

template <typename Input, typename Output, typename Func, bool End>
struct pipeline_stage_coro {
  using output_t = Output;

  tmc::chan_tok<Output> outChan_;
  tmc::semaphore outSem_;

  pipeline_stage_coro(
    tmc::chan_tok<Input> inChan, tmc::semaphore* inSem, Func func,
    size_t workerCount
  )
      : outChan_(tmc::make_channel<Output>()),
        outSem_(tmc::semaphore(tmc::semaphore::half_word(2 * workerCount))) {
    tmc::semaphore* outSem = &outSem_;
    if constexpr (End) {
      outSem = nullptr;
    }

    auto sg = tmc::spawn_group();
    for (size_t i = 0; i < workerCount; ++i) {
      sg.add(worker_coro(inChan, inSem, outChan_, outSem, func));
    }

    // Create a task that automatically closes the output channel after all of
    // the workers finish.
    tmc::spawn([](auto SG, auto Chan) -> tmc::task<void> {
      co_await std::move(SG);
      co_await Chan.drain();
    }(std::move(sg), outChan_))
      .detach();
  }

  tmc::chan_tok<Output> get_channel() { return outChan_; }
};

template <typename Input, typename Func>
auto start_pipeline_coro(
  tmc::chan_tok<Input> from, Func transformFunc, size_t workerCount = 1
) {
  using Output =
    tmc::detail::awaitable_result_t<std::invoke_result_t<Func, Input>>;
  return pipeline_stage_coro<Input, Output, Func, false>{
    from, nullptr, transformFunc, workerCount
  };
}

template <typename PriorStage, typename Func>
auto pipeline_transform_coro(
  PriorStage& from, Func transformFunc, size_t workerCount = 1
) {
  using Input = PriorStage::output_t;
  using Output =
    tmc::detail::awaitable_result_t<std::invoke_result_t<Func, Input>>;
  return pipeline_stage_coro<Input, Output, Func, false>{
    from.outChan_, &from.outSem_, transformFunc, workerCount
  };
}

template <typename PriorStage, typename Func>
auto end_pipeline_coro(
  PriorStage& from, Func transformFunc, size_t workerCount = 1
) {
  using Input = PriorStage::output_t;
  using Output =
    tmc::detail::awaitable_result_t<std::invoke_result_t<Func, Input>>;
  return pipeline_stage_coro<Input, Output, Func, true>{
    from.outChan_, &from.outSem_, transformFunc, workerCount
  };
}

static float plus_half(int i) { return static_cast<float>(i) + 0.5f; }
static tmc::task<double> times_two(float i) {
  co_return static_cast<double>(2.0f * i);
}
static int minus_one(double i) { return static_cast<int>(i) - 1; }
static bool as_bool(double i) { return i > 2; }

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
  return tmc::async_main([]() -> tmc::task<int> {
    std::printf("testing %d items through 4-stage pipeline...\n", NELEMS);
    auto constructStart = std::chrono::high_resolution_clock::now();

    auto in = tmc::make_channel<int>();
    auto first = start_pipeline_func(in, plus_half, 10);
    auto second = pipeline_transform_coro(first, times_two, 10);
    auto third = pipeline_transform_func(second, minus_one, 10);
    auto out = end_pipeline_func(third, as_bool, 10);

    auto consumer = tmc::spawn(
                      [](
                        tmc::chan_tok<bool> outChan
                      ) -> tmc::task<std::tuple<size_t, size_t>> {
                        size_t sum = 0;
                        size_t count = 0;
                        while (auto data = co_await outChan.pull()) {
                          sum += data.value();
                          ++count;
                        }
                        co_return std::tuple<size_t, size_t>{sum, count};
                      }(out.get_channel())
    )
                      .fork();

    auto submitStart = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NELEMS; ++i) {
      in.post(i);
    }

    auto processStart = std::chrono::high_resolution_clock::now();

    co_await in.drain();
    auto [sum, count] = co_await std::move(consumer);

    auto processEnd = std::chrono::high_resolution_clock::now();

    std::printf("element sum: %zu\n", sum);
    std::printf("element count: %zu\n", count);

    size_t constructTime =
      static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            submitStart - constructStart
      )
                            .count());

    size_t submitTime =
      static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            processStart - submitStart
      )
                            .count());

    size_t processTime =
      static_cast<size_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                            processEnd - processStart
      )
                            .count());

    std::printf("construct time: %f ms\n", static_cast<double>(constructTime));
    std::printf("submit time: %f ms\n", static_cast<double>(submitTime));
    std::printf("process time: %f ms\n", static_cast<double>(processTime));
    std::printf("%s elements/sec\n", formatElementsPerSec(processTime).c_str());
    co_return 0;
  }());
}

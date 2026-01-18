// A parallel actor-based data pipeline with:
// - Processing functions can be regular functions or coroutines
// - Configurable number of stages / input / output types
// - Configurable number of workers per stage
// - Automatic backpressure based on the number of workers in the current stage

// This pipeline may process tasks out of order. For a FIFO pipeline, see
// pipeline_fifo.cpp.

#include "tmc/channel.hpp"
#include "tmc/fork_group.hpp"
#include "tmc/semaphore.hpp"
#include "tmc/spawn_group.hpp"
#include "tmc/task.hpp"
#include "tmc/traits.hpp"

#include <cstddef>
#include <type_traits>

template <typename Input, typename Output, typename ProcessFunc>
struct pipeline_stage {
  using output_t = Output;

  tmc::chan_tok<Output> outChan_;
  tmc::semaphore outSem_;

  static tmc::task<void> worker(
    tmc::chan_tok<Input> inChan, tmc::semaphore* inSem,
    tmc::chan_tok<Output> outChan, tmc::semaphore* outSem, ProcessFunc func
  ) {
    while (auto input = co_await inChan.pull()) {
      if (inSem != nullptr) {
        inSem->release();
      }

      if (outSem != nullptr) {
        co_await *outSem;
      }

      if constexpr (tmc::traits::is_awaitable<
                      std::invoke_result_t<ProcessFunc, Input&&>>) {
        // ProcessFunc is a coroutine
        outChan.post(co_await func(std::move(*input)));
      } else {
        // ProcessFunc is a regular function
        outChan.post(func(std::move(*input)));
      }
    }
  }

  pipeline_stage(
    tmc::aw_fork_group<0, void>& fg, tmc::chan_tok<Input> inChan,
    tmc::semaphore* inSem, ProcessFunc func, size_t workerCount
  )
      : outChan_(tmc::make_channel<Output>()),
        // Initialize our output semaphore to 0. The next stage will set this
        // semaphore capacity (for their input channel) based on their worker
        // count.
        outSem_(tmc::semaphore(0)) {

    // Set our input channel capacity to 2x our worker count.
    if (inSem != nullptr) {
      inSem->release(2 * workerCount);
    }

    auto sg = tmc::spawn_group();
    for (size_t i = 0; i < workerCount; ++i) {
      sg.add(worker(inChan, inSem, outChan_, &outSem_, func));
    }

    // Create a task that automatically closes and drains the output channel
    // after all of the workers finish.
    fg.fork([](auto SG, auto Chan) -> tmc::task<void> {
      co_await std::move(SG);
      co_await Chan.drain(); // implicitly closes the channel
    }(std::move(sg), outChan_));
  }

  tmc::chan_tok<Output> get_channel() { return outChan_; }
};

template <typename Input, typename ProcessFunc> struct pipeline_end_stage {
  static tmc::task<void>
  worker(tmc::chan_tok<Input> inChan, tmc::semaphore* inSem, ProcessFunc func) {
    while (auto input = co_await inChan.pull()) {
      inSem->release();

      if constexpr (tmc::traits::is_awaitable<
                      std::invoke_result_t<ProcessFunc, Input&&>>) {
        // ProcessFunc is a coroutine - await it and ignore result
        co_await func(std::move(*input));
      } else {
        // ProcessFunc is a regular function - call it and ignore result
        func(std::move(*input));
      }
    }
  }

  pipeline_end_stage(
    tmc::aw_fork_group<0, void>& fg, tmc::chan_tok<Input> inChan,
    tmc::semaphore* inSem, ProcessFunc func, size_t workerCount
  ) {
    // Set our input channel capacity to 2x our worker count.
    inSem->release(2 * workerCount);

    auto sg = tmc::spawn_group();
    for (size_t i = 0; i < workerCount; ++i) {
      sg.add(worker(inChan, inSem, func));
    }

    fg.fork(std::move(sg));
  }
};

template <typename Input, typename Func>
auto start_pipeline(
  tmc::aw_fork_group<0, void>& fg, tmc::chan_tok<Input> from,
  tmc::semaphore* inSem, Func transformFunc, size_t workerCount = 1
) {
  using Intermediate = std::invoke_result_t<Func, Input&&>;
  using Output = std::conditional_t<
    tmc::traits::is_awaitable<Intermediate>,
    tmc::traits::awaitable_result_t<Intermediate>, Intermediate>;
  return pipeline_stage<Input, Output, Func>{
    fg, from, inSem, transformFunc, workerCount
  };
}

template <typename PriorStage, typename Func>
auto pipeline_transform(
  tmc::aw_fork_group<0, void>& fg, PriorStage& from, Func transformFunc,
  size_t workerCount = 1
) {
  using Input = typename PriorStage::output_t;
  using Intermediate = std::invoke_result_t<Func, Input&&>;
  using Output = std::conditional_t<
    tmc::traits::is_awaitable<Intermediate>,
    tmc::traits::awaitable_result_t<Intermediate>, Intermediate>;
  return pipeline_stage<Input, Output, Func>{
    fg, from.outChan_, &from.outSem_, transformFunc, workerCount
  };
}

template <typename PriorStage, typename Func>
auto end_pipeline(
  tmc::aw_fork_group<0, void>& fg, PriorStage& from, Func consumeFunc,
  size_t workerCount = 1
) {
  using Input = typename PriorStage::output_t;
  return pipeline_end_stage<Input, Func>{
    fg, from.outChan_, &from.outSem_, consumeFunc, workerCount
  };
}

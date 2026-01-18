// A parallel actor-based data pipeline with:
// - Processing functions can be regular functions or coroutines
// - Configurable number of stages / input / output types
// - Configurable parallelism per stage
// - Automatic backpressure based on the current stage parallelism

// This pipeline implements guaranteed FIFO processing throughout. Each stage
// has a single main worker that awaits data from the prior stage's channel, and
// forks a new task worker to handle the processing of the next stage. A handle
// to this forked task is placed into the next stage's input channel. This
// allows multiple forked workers to execute in parallel, but the next stage
// will only retrieve results in the order they were originally submitted.

// Since forked tasks are non-movable this uses the with_result_of trick to
// construct them directly into channel storage, and consumers access the
// channel using the pull_zc() zero-copy read function.

// One quirk of this implementation is that the final consumer must also read
// results by co_awaiting the output of pull_zc().

#include "tmc/channel.hpp"
#include "tmc/fork_group.hpp"
#include "tmc/semaphore.hpp"
#include "tmc/spawn.hpp"
#include "tmc/task.hpp"
#include "tmc/traits.hpp"

#include <cstddef>
#include <type_traits>

// Required to allow constructing the non-movable type tmc::aw_spawn_fork
// directly into the channel storage
// https://quuxplusone.github.io/blog/2018/05/17/super-elider-round-2/
template <class F> class with_result_of_t {
  F&& fun;

public:
  using T = decltype(std::declval<F&&>()());
  explicit with_result_of_t(F&& f) : fun(std::forward<F>(f)) {}
  operator T() noexcept { return fun(); }
};

template <class F> inline with_result_of_t<F> with_result_of(F&& f) {
  return with_result_of_t<F>(std::forward<F>(f));
}

template <typename Input, typename Output, typename ProcessFunc, bool End>
struct pipeline_stage {
  // The output channel contains forked task handles - this is how we manage
  // parallelism while ensuring FIFO ordering.
  using output_t = tmc::aw_spawn_fork<tmc::task<Output>>;

  tmc::chan_tok<output_t> outChan_;
  tmc::semaphore outSem_;

  template <typename CInput>
  static tmc::task<void> worker(
    tmc::chan_tok<CInput> inChan, tmc::semaphore* inSem,
    tmc::chan_tok<output_t> outChan, tmc::semaphore* outSem, ProcessFunc func
  ) {
    while (auto input = co_await inChan.pull_zc()) {
      if (inSem != nullptr) {
        inSem->release();
      }

      // Wait for the output semaphore so we can construct the data directly in
      // the output channel
      if (outSem != nullptr) {
        co_await *outSem;
      }

      if constexpr (tmc::traits::is_awaitable<CInput>) {
        auto data = co_await std::move(input->get());
        using FInput = tmc::traits::awaitable_result_t<CInput>;
        // The data element in the channel is an awaitable
        if constexpr (tmc::traits::is_awaitable<
                        std::invoke_result_t<ProcessFunc, FInput&&>>) {
          // ProcessFunc is a coroutine
          outChan.post(with_result_of([&]() {
            return tmc::spawn(func(std::move(data))).fork();
          }));
        } else {
          // ProcessFunc is a regular function
          outChan.post(with_result_of([&]() {
            return tmc::spawn([](ProcessFunc f, FInput i) -> tmc::task<Output> {
                     co_return f(std::move(i));
                   }(func, std::move(data)))
              .fork();
          }));
        }
      } else {
        using FInput = CInput;
        // The data element in the channel is a value
        if constexpr (tmc::traits::is_awaitable<
                        std::invoke_result_t<ProcessFunc, FInput&&>>) {
          // ProcessFunc is a coroutine
          outChan.post(with_result_of([&]() {
            return tmc::spawn(func(std::move(input->get()))).fork();
          }));
        } else {
          // ProcessFunc is a regular function
          outChan.post(with_result_of([&]() {
            return tmc::spawn([](ProcessFunc f, FInput i) -> tmc::task<Output> {
                     co_return f(std::move(i));
                   }(func, std::move(input->get())))
              .fork();
          }));
        }
      }
    }
  }

  pipeline_stage(
    tmc::aw_fork_group<0, void>& fg, tmc::chan_tok<Input> inChan,
    tmc::semaphore* inSem, ProcessFunc func, size_t parallelism
  )
      : outChan_(tmc::make_channel<tmc::aw_spawn_fork<tmc::task<Output>>>()),
        // Initialize our output semaphore to 0. The next stage will set this
        // semaphore capacity (for their input channel) based on their worker
        // count.
        outSem_(tmc::semaphore(0)) {

    // Set our input channel capacity to 2x our worker count.
    if (inSem != nullptr) {
      inSem->release(2 * parallelism);
    }

    tmc::semaphore* outSem = &outSem_;
    if constexpr (End) {
      outSem = nullptr;
    }

    // Create a task that automatically closes and drains the output channel
    // after all of the workers finish.
    fg.fork([](tmc::task<void> T, auto Chan) -> tmc::task<void> {
      co_await std::move(T);
      co_await Chan.drain(); // implicitly closes the channel
    }(worker(inChan, inSem, outChan_, outSem, func), outChan_));
  }

  tmc::chan_tok<output_t> get_channel() { return outChan_; }
};

template <typename Input, typename Func>
auto start_pipeline(
  tmc::aw_fork_group<0, void>& fg, tmc::chan_tok<Input> from,
  tmc::semaphore* inSem, Func transformFunc, size_t parallelism = 1
) {
  using Intermediate = std::invoke_result_t<Func, Input&&>;
  using Output = std::conditional_t<
    tmc::traits::is_awaitable<Intermediate>,
    tmc::traits::awaitable_result_t<Intermediate>, Intermediate>;
  return pipeline_stage<Input, Output, Func, false>{
    fg, from, inSem, transformFunc, parallelism
  };
}

template <typename PriorStage, typename Func>
auto pipeline_transform(
  tmc::aw_fork_group<0, void>& fg, PriorStage& from, Func transformFunc,
  size_t parallelism = 1
) {
  using Input = typename PriorStage::output_t;
  using IntermediateInput = std::conditional_t<
    tmc::traits::is_awaitable<Input>, tmc::traits::awaitable_result_t<Input>,
    Input>;
  using IntermediateOutput = std::invoke_result_t<Func, IntermediateInput&&>;
  using Output = std::conditional_t<
    tmc::traits::is_awaitable<IntermediateOutput>,
    tmc::traits::awaitable_result_t<IntermediateOutput>, IntermediateOutput>;
  return pipeline_stage<Input, Output, Func, false>{
    fg, from.outChan_, &from.outSem_, transformFunc, parallelism
  };
}

template <typename Input, typename ProcessFunc> struct pipeline_end_stage {
  template <typename CInput>
  static tmc::task<void> worker(
    tmc::chan_tok<CInput> inChan, tmc::semaphore* inSem, ProcessFunc func
  ) {
    while (auto input = co_await inChan.pull_zc()) {
      inSem->release();

      if constexpr (tmc::traits::is_awaitable<CInput>) {
        auto data = co_await std::move(input->get());
        using FInput = tmc::traits::awaitable_result_t<CInput>;
        if constexpr (tmc::traits::is_awaitable<
                        std::invoke_result_t<ProcessFunc, FInput&&>>) {
          co_await func(std::move(data));
        } else {
          func(std::move(data));
        }
      } else {
        using FInput = CInput;
        if constexpr (tmc::traits::is_awaitable<
                        std::invoke_result_t<ProcessFunc, FInput&&>>) {
          co_await func(std::move(input->get()));
        } else {
          func(std::move(input->get()));
        }
      }
    }
  }

  pipeline_end_stage(
    tmc::aw_fork_group<0, void>& fg, tmc::chan_tok<Input> inChan,
    tmc::semaphore* inSem, ProcessFunc func, size_t parallelism
  ) {
    // Set our input channel capacity to 2x our worker count.
    inSem->release(2 * parallelism);

    fg.fork(worker(inChan, inSem, func));
  }
};

template <typename PriorStage, typename Func>
auto end_pipeline(
  tmc::aw_fork_group<0, void>& fg, PriorStage& from, Func consumeFunc,
  size_t parallelism = 1
) {
  using Input = typename PriorStage::output_t;
  return pipeline_end_stage<Input, Func>{
    fg, from.outChan_, &from.outSem_, consumeFunc, parallelism
  };
}

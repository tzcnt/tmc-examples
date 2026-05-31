// A parallel actor-based data pipeline with:
// - Processing functions can be regular functions or coroutines
// - Configurable number of stages / input / output types
// - Configurable parallelism per stage
// - Automatic backpressure based on the current stage parallelism

// This pipeline implements guaranteed FIFO processing throughout. Each stage
// has a single main worker that awaits data from the prior stage's queue, and
// forks a new task worker to handle the processing of the next stage. A handle
// to this forked task is placed into the next stage's input queue. This
// allows multiple forked workers to execute in parallel, but the next stage
// will only retrieve results in the order they were originally submitted.

// Since forked tasks are non-movable this uses the with_result_of trick to
// construct them directly into queue storage, and consumers access the
// queue using the pull() zero-copy read function.

// One quirk of this implementation is that the final consumer must also read
// results by co_awaiting the output of pull().

// Each stage owns its own output queue, sized to that stage's parallelism
// parameter. The bounded output queue gates the stage's own in-flight
// parallelism: when the queue is full, the stage's worker blocks on push(),
// preventing it from forking more work than `parallelism` items at a time.
// The start stage owns its input queue (the queue the driver pushs to) in
// addition to its output queue, and exposes it as the public `in` member.

#include "tmc/fork_group.hpp"
#include "tmc/qu_spsc_bounded.hpp"
#include "tmc/spawn.hpp"
#include "tmc/task.hpp"
#include "tmc/traits.hpp"

#include <cstddef>
#include <memory>
#include <type_traits>

// Required to allow constructing the non-movable type tmc::aw_spawn_fork
// directly into the queue storage
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

// qu_spsc_bounded is non-movable and non-copyable, so we share it via
// shared_ptr to keep it alive for the lifetime of the single producer and
// single consumer.
struct pipeline_queue_config : tmc::qu_spsc_bounded_default_config {};
template <typename T>
using pipeline_queue = tmc::qu_spsc_bounded<T, pipeline_queue_config>;
template <typename T>
using pipeline_queue_ptr = std::shared_ptr<pipeline_queue<T>>;

template <typename T> pipeline_queue_ptr<T> make_pipeline_queue(size_t size) {
  return std::make_shared<pipeline_queue<T>>(size);
}

template <typename Input, typename Output, typename ProcessFunc>
struct pipeline_stage {
  // The output queue contains forked task handles - this is how we manage
  // parallelism while ensuring FIFO ordering.
  using output_t = tmc::aw_spawn_fork<tmc::task<Output>>;

  // This stage owns its own output queue. It is sized to `parallelism` so
  // that at most `parallelism` forked task handles can be in flight: when
  // the queue is full, `co_await outQueue->push()` suspends, preventing
  // further forks until the downstream stage drains an entry.
  pipeline_queue_ptr<output_t> output_queue;

  template <typename CInput>
  static tmc::task<void> worker(
    pipeline_queue_ptr<CInput> inQueue, pipeline_queue_ptr<output_t> outQueue,
    ProcessFunc func
  ) {
    while (auto input = co_await inQueue->pull()) {
      if constexpr (tmc::traits::is_awaitable<CInput>) {
        auto data = co_await std::move(input.get());
        using FInput = tmc::traits::awaitable_result_t<CInput>;
        // The data element in the queue is an awaitable
        if constexpr (tmc::traits::is_awaitable<
                        std::invoke_result_t<ProcessFunc, FInput&&>>) {
          // ProcessFunc is a coroutine
          co_await outQueue->push(with_result_of([&]() {
            return tmc::spawn(func(std::move(data))).fork();
          }));
        } else {
          // ProcessFunc is a regular function
          co_await outQueue->push(with_result_of([&]() {
            return tmc::spawn([](ProcessFunc f, FInput i) -> tmc::task<Output> {
                     co_return f(std::move(i));
                   }(func, std::move(data)))
              .fork();
          }));
        }
      } else {
        using FInput = CInput;
        // The data element in the queue is a value
        if constexpr (tmc::traits::is_awaitable<
                        std::invoke_result_t<ProcessFunc, FInput&&>>) {
          // ProcessFunc is a coroutine
          co_await outQueue->push(with_result_of([&]() {
            return tmc::spawn(func(std::move(input.get()))).fork();
          }));
        } else {
          // ProcessFunc is a regular function
          co_await outQueue->push(with_result_of([&]() {
            return tmc::spawn([](ProcessFunc f, FInput i) -> tmc::task<Output> {
                     co_return f(std::move(i));
                   }(func, std::move(input.get())))
              .fork();
          }));
        }
      }
    }

    // Input queue is closed and drained. Close the output queue so the
    // downstream stage knows no more elements are coming.
    outQueue->close();
  }

  pipeline_stage(
    tmc::aw_fork_group<0, void>& fg, pipeline_queue_ptr<Input> inQueue,
    ProcessFunc func, size_t parallelism
  )
      : output_queue(make_pipeline_queue<output_t>(parallelism)) {
    fg.fork(worker(inQueue, output_queue, func));
  }

  pipeline_queue_ptr<output_t> get_queue() { return output_queue; }
};

// The start stage is a normal pipeline_stage that additionally owns its
// input queue and exposes it as the public `in` member, so the driver has
// somewhere to push the initial elements.
template <typename Input, typename Output, typename ProcessFunc>
struct pipeline_start_stage : pipeline_stage<Input, Output, ProcessFunc> {
  pipeline_queue_ptr<Input> input_queue;

  pipeline_start_stage(
    tmc::aw_fork_group<0, void>& fg, pipeline_queue_ptr<Input> inQueue,
    ProcessFunc func, size_t parallelism
  )
      : pipeline_stage<Input, Output, ProcessFunc>(
          fg, inQueue, func, parallelism
        ),
        input_queue(std::move(inQueue)) {}
};

// Creates the first stage of the pipeline. The driver writes input elements
// to the returned stage's `in` member. The Input type must be specified
// explicitly as a template argument.
template <typename Input, typename Func>
auto start_pipeline(
  tmc::aw_fork_group<0, void>& fg, Func transformFunc, size_t parallelism = 1
) {
  using Intermediate = std::invoke_result_t<Func, Input&&>;
  using Output = std::conditional_t<
    tmc::traits::is_awaitable<Intermediate>,
    tmc::traits::awaitable_result_t<Intermediate>, Intermediate>;
  // The input queue is sized to `parallelism` to give the driver buffering
  // proportional to the stage's processing capacity.
  auto inQueue = make_pipeline_queue<Input>(parallelism);
  return pipeline_start_stage<Input, Output, Func>{
    fg, std::move(inQueue), transformFunc, parallelism
  };
}

// Adds an intermediate stage that pulls from the prior stage's output queue
// and forwards through its own output queue (sized to `parallelism`).
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
  return pipeline_stage<Input, Output, Func>{
    fg, from.get_queue(), transformFunc, parallelism
  };
}

template <typename Input, typename ProcessFunc> struct pipeline_end_stage {
  // Fast path used when parallelism == 1: process each item inline in the
  // consumer task, avoiding the internal queue and fork overhead entirely.
  template <typename CInput>
  static tmc::task<void>
  inline_worker(pipeline_queue_ptr<CInput> inQueue, ProcessFunc func) {
    while (auto input = co_await inQueue->pull()) {
      if constexpr (tmc::traits::is_awaitable<CInput>) {
        auto data = co_await std::move(input.get());
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
          co_await func(std::move(input.get()));
        } else {
          func(std::move(input.get()));
        }
      }
    }
  }

  template <typename CInput>
  static tmc::task<void> worker(
    pipeline_queue_ptr<CInput> inQueue, ProcessFunc func, size_t parallelism
  ) {
    // Each in-flight consume runs as its own forked task. The internal
    // bounded queue holds those fork handles so we can await them in FIFO
    // order. Sized to `parallelism` - exactly large enough to hold all
    // currently in-flight tasks.
    using consume_fork_t = tmc::aw_spawn_fork<tmc::task<void>>;
    auto internalQueue = make_pipeline_queue<consume_fork_t>(parallelism);

    // Track the number of currently active (in-flight) consume tasks in
    // a separate size_t. This lets us drain exactly when we hit the
    // parallelism limit, so the next push() never blocks on a full queue.
    size_t active = 0;

    while (auto input = co_await inQueue->pull()) {
      if constexpr (tmc::traits::is_awaitable<CInput>) {
        // Upstream queue contains forked task handles - await to get value.
        auto data = co_await std::move(input.get());
        using FInput = tmc::traits::awaitable_result_t<CInput>;
        co_await internalQueue->push(with_result_of([&]() {
          return tmc::spawn([](ProcessFunc f, FInput d) -> tmc::task<void> {
                   if constexpr (tmc::traits::is_awaitable<std::invoke_result_t<
                                   ProcessFunc, FInput&&>>) {
                     co_await f(std::move(d));
                   } else {
                     f(std::move(d));
                   }
                   co_return;
                 }(func, std::move(data)))
            .fork();
        }));
      } else {
        using FInput = CInput;
        // Upstream queue contains values directly.
        co_await internalQueue->push(with_result_of([&]() {
          return tmc::spawn([](ProcessFunc f, FInput d) -> tmc::task<void> {
                   if constexpr (tmc::traits::is_awaitable<std::invoke_result_t<
                                   ProcessFunc, FInput&&>>) {
                     co_await f(std::move(d));
                   } else {
                     f(std::move(d));
                   }
                   co_return;
                 }(func, std::move(input.get())))
            .fork();
        }));
      }
      ++active;

      // Hit the parallelism limit - drain one completion (in FIFO order)
      // before the next iteration's push() could otherwise block.
      if (active >= parallelism) {
        auto handle = co_await internalQueue->pull();
        co_await std::move(handle.get());
        --active;
      }
    }

    // Upstream is closed. Drain any remaining in-flight forks in order.
    while (active > 0) {
      auto handle = co_await internalQueue->pull();
      co_await std::move(handle.get());
      --active;
    }
  }

  pipeline_end_stage(
    tmc::aw_fork_group<0, void>& fg, pipeline_queue_ptr<Input> inQueue,
    ProcessFunc func, size_t parallelism
  ) {
    if (parallelism == 1) {
      fg.fork(inline_worker(inQueue, func));
    } else {
      fg.fork(worker(inQueue, func, parallelism));
    }
  }
};

// The terminal consumer of the pipeline. With `parallelism` > 1 it runs
// up to that many consume() invocations concurrently. Completion of those
// forked tasks is awaited in FIFO order, but the consume() bodies
// themselves execute in parallel - if `consumeFunc` touches shared state,
// the caller is responsible for synchronization.
template <typename PriorStage, typename Func>
auto end_pipeline(
  tmc::aw_fork_group<0, void>& fg, PriorStage& from, Func consumeFunc,
  size_t parallelism = 1
) {
  using Input = typename PriorStage::output_t;
  return pipeline_end_stage<Input, Func>{
    fg, from.get_queue(), consumeFunc, parallelism
  };
}

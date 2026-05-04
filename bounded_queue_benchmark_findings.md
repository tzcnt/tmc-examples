# `bounded_queue` Benchmark Findings

This note summarizes the benchmark and `perf` investigation for:

- `./build/clang-linux-release/chan_bench`
- `./build/clang-linux-release/bounded_queue_bench`

Both binaries were run in their current 1 producer / 1 consumer / 10 iteration form.

## Benchmark Summary

Observed throughput on the current binaries:

- `chan_bench`: roughly `39M` to `54M` elements/sec, `overall: 2.12 sec`
- `bounded_queue_bench`: roughly `18M` to `21M` elements/sec, `overall: 5.10 sec`

`perf stat` also showed that `bounded_queue_bench` is spending much more time stalled on synchronization and cache movement:

- `chan_bench`: `13.8B` cycles, `22.7B` instructions, `1.65` IPC, `82M` cache misses
- `bounded_queue_bench`: `34.7B` cycles, `16.6B` instructions, `0.48` IPC, `221M` cache misses

That points to extra locked operations / cache-line bouncing in the bounded-queue fast path, not to hazard pointers being the dominant cost in `channel`.

## Hot Lines In `bounded_queue`

The table below lists the lines that matter most for the current slowdown.

| Current `bounded_queue` line(s) | Why it is hot | `channel` equivalent | Equivalent? | Present before `close()`? |
| --- | --- | --- | --- | --- |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:363-366` | `write_state.compare_exchange_weak(...)` in `begin_push()` runs on every push. | `submodules/TooManyCooks/include/tmc/channel.hpp:1010-1013` uses `write_offset.fetch_add(...)` plus a block load. | No. `channel` uses a single `fetch_add`; `bounded_queue` now uses a CAS loop because the closed bit is packed into the write counter. | No. New in commit `5541a5f`. Pre-close code used `write_count.fetch_add(...)` at `HEAD~1 submodules/TooManyCooks/include/tmc/bounded_queue.hpp:289-291`. |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:376` | `read_count.fetch_add(...)` in `begin_pull()` runs on every pull. | `submodules/TooManyCooks/include/tmc/channel.hpp:1114-1116` uses `read_offset.fetch_add(...)`. | Yes, broadly. Both queues claim a read ticket with a per-pull atomic increment. | Yes. Present before close at `HEAD~1 submodules/TooManyCooks/include/tmc/bounded_queue.hpp:295-297`. |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:461-464` | `Elem.turn.compare_exchange_strong(...)` in `complete_push()` now publishes each element with a locked CAS. | `submodules/TooManyCooks/include/tmc/channel.hpp:392-397` or `submodules/TooManyCooks/include/tmc/channel.hpp:455-460` publish channel data with `flags.compare_exchange_strong(...)`. | Roughly. Both are producer-side publish CAS operations, but `channel` already needed this for its element state machine while pre-close `bounded_queue` did not. | No. New in commit `5541a5f`. Pre-close code used `Elem.turn.store(PublishTurn, ...)` at `HEAD~1 submodules/TooManyCooks/include/tmc/bounded_queue.hpp:311`. |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:469` | `active_values.fetch_add(...)` adds a producer/consumer-shared atomic update for every item. | No direct fast-path equivalent. `channel` producer-side release is a plain store to the hazard pointer state at `submodules/TooManyCooks/include/tmc/channel.hpp:1247-1250`. | No. `channel` has close/drain bookkeeping, but not a per-item shared counter like this in the steady-state push path. | No. New in commit `5541a5f`. |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:333` | `Elem.waiter.exchange(nullptr, ...)` in `wake_waiter()` is paid on every publish and every release, even when no waiter is present. | No exact equivalent. The nearest producer-side state publication is `submodules/TooManyCooks/include/tmc/channel.hpp:392-397` or `submodules/TooManyCooks/include/tmc/channel.hpp:455-460`, but `channel` does not unconditionally exchange a waiter pointer on both producer and consumer completion paths. | No. `bounded_queue` pays this unconditionally; `channel` uses a different element state machine. | Yes. Present before close at `HEAD~1 submodules/TooManyCooks/include/tmc/bounded_queue.hpp:278-285`. |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:441-442` | The non-cancelled `complete_release()` path still does `turn.store(...)` plus `wake_waiter(...)` for every pull completion. | Consumer release in `channel` is a plain hazard-pointer store at `submodules/TooManyCooks/include/tmc/channel.hpp:263-265` for zero-copy or `submodules/TooManyCooks/include/tmc/channel.hpp:1429-1432` for regular pull. | No. `channel` consumer release is much cheaper in the steady state. | Partly. The old behavior was `Elem.turn.store(...)` plus `wake_waiter(...)` at `HEAD~1 submodules/TooManyCooks/include/tmc/bounded_queue.hpp:321-322`. The cancellation branch around it was added by `5541a5f`. |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:481` | `active_values.fetch_sub(...)` adds another per-item shared atomic update on the consumer side. | No direct fast-path equivalent. `channel` consumer release is `active_offset.store(...)` at `submodules/TooManyCooks/include/tmc/channel.hpp:263-265` or `submodules/TooManyCooks/include/tmc/channel.hpp:1429-1432`. | No. This is extra bounded-queue bookkeeping for `drain()`. | No. New in commit `5541a5f`. |
| `submodules/TooManyCooks/include/tmc/bounded_queue.hpp:487-497` | `finish_pull()` adds close-aware control flow on the pull fast path: retry/cancel/closed checks and possible re-ticketing. `perf` attributes a large share of consumer-side time to this function. | `submodules/TooManyCooks/include/tmc/channel.hpp:1130-1151` and then `submodules/TooManyCooks/include/tmc/channel.hpp:1309-1314` handle close-aware end-of-stream for `channel`. | Roughly. Both queues need close-aware read termination, but `bounded_queue` now does it in an explicit post-await loop on every pull completion. | No. New in commit `5541a5f`. |

## What `perf` Actually Pointed At

The hottest symbols in the bounded-queue profile were:

- `producer(tmc::bounded_queue<...>&, ...) [clone .resume]`
- `tmc::bounded_queue<...>::finish_pull(...)`
- `consumer(tmc::bounded_queue<...>&) [clone .resume]`

`perf annotate` for `finish_pull()` showed the time is not in close-only teardown code. It is spent in the steady-state path that checks slot state, wakes the other side, and updates `active_values`.

The channel profile did not show hazard pointer maintenance dominating the runtime. Its hot work was mostly:

- `submodules/TooManyCooks/include/tmc/channel.hpp:1010-1043` in `get_write_ticket()`
- `submodules/TooManyCooks/include/tmc/channel.hpp:1114-1169` in `get_read_ticket()`
- `submodules/TooManyCooks/include/tmc/channel.hpp:1179-1184` in `write_element()`
- `submodules/TooManyCooks/include/tmc/channel.hpp:1305-1402` in `aw_pull_base_impl::await_ready()`

In other words: the slowdown is not explained by "channel uses hazard pointers". The bounded queue is currently paying more per-item synchronization than the channel in this specific benchmark.

## Newly Added Costs From `close()` / `drain()`

The close/drain commit `5541a5f` introduced the following steady-state costs that were not in the original bounded queue:

- Packing the closed bit into the write counter, which changed `begin_push()` from `fetch_add` to a CAS loop.
- Cancellation-aware publish, which changed producer publication from `turn.store(...)` to `turn.compare_exchange_strong(...)`.
- The `active_values` counter, which adds one extra atomic RMW on every successful push and one on every successful pull.
- Extra close-aware checks in `push_can_complete()`, `pull_can_complete()`, and `finish_pull()`.

These are the main regressions introduced by the close/drain work.

## Pre-Close Costs That Look Fixable

The following costs were already present before `close()`, but they do not look fundamentally required in their current form:

- The unconditional `waiter.exchange(nullptr, ...)` in `wake_waiter()`. This pays a locked RMW even when no waiter is present, and looks like avoidable overhead rather than a semantic requirement.
- The lack of a cheaper specialized 1P/1C fast path. `bounded_queue` always pays the full general waiter/turn protocol, while `channel`'s element state machine ends up cheaper in this benchmark.
- The pull-side ticket claim `read_count.fetch_add(...)`. A reusable MPMC queue does need a way to claim read ownership, but this exact shape is also one of the places where bounded_queue simply inherits the same style of per-operation ticketing as `channel` rather than taking advantage of the narrower ring-buffer design.

These are not close/drain regressions, but they are plausible optimization targets.

## Pre-Close Costs Required For Correctness

The following costs were already present before `close()` and appear to come from the basic correctness model of this queue:

- The slot waiter installation path in `suspend_for_turn(...)`. If producer and consumer arrive out of order, one side must register itself somewhere so the other side can wake it.
- The slot-turn handoff model where producer completion publishes a per-slot turn and consumer completion advances that turn again for reuse.
- The per-operation ticketing structure in general. A reusable bounded MPMC ring needs a way to distinguish different generations of the same slot; the turn values and ticket-derived slot ownership are how this implementation does that safely.

These costs may still be reducible in constant-factor terms, but some version of them is required for a correct reusable bounded queue with this API and concurrency model.

## Bottom Line

The current bounded queue is slower than `channel` for this 1P/1C case for two reasons:

- Some nontrivial steady-state costs were already present in the slot-turn / waiter-exchange design.
- The `close()` / `drain()` commit added several more fast-path atomics and branches, especially the `write_state` CAS loop, the publish CAS, and the `active_values` counter.

So the answer is not "all of the bottleneck came from close" and not "none of it did". The close/drain commit introduced several of the most important new bottlenecks, but some measurable overhead was already in the original bounded-queue fast path.

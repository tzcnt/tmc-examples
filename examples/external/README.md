Examples that demonstrate how to build integrations between TMC and external libraries.

- external_awaitable: How to construct your own TMC awaitable by implementing tmc::detail::awaitable_traits.
- external_executor: How to construct your own TMC executor by implementing tmc::detail::executor_traits.
- callback_awaitable: How to wrap a C-style callback API provided by another library into a TMC awaitable.

- external_coro : This external coroutine type has no special capabilities. Demonstrates how it can be used from TMC.
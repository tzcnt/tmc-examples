New name: mini_mutex ?

This is useful when you have async operations where you need to serialize the initiation of the operation, but not the completion / post-handler phase.

The mutex will be held until the awaited coroutine returns:
- when the coroutine finishes
- when await_suspend of an awaitable returns
If the awaited coroutine symmetric transfers to another coroutine, the mutex will not unlock until that coroutine suspends or finishes.

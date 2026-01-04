Examples that make use of the hwloc integration.
These examples are CMake build targets with the hwloc_ prefix.

- topo.cpp: Prints the system topology as TMC views it.
- hybrid_executor.cpp: Demonstrates work steering based on priority on hybrid CPUs.

Examples demonstrating Asio sharding using SO_REUSEADDR / SO_REUSEPORT:
- asio_thread_per_core.cpp: Creates an isolated, pinned Asio thread per core. This is similar to the "share-nothing" architecture used by thread-per-core systems.
- asio_thread_per_core_prefork.sh: Runs the prior, with each thread in its own process, and each process pinned to a different core.

- asio_server_per_core.cpp: Creates an isolated, pinned Asio thread per core, and a CPU executor thread pinned to the same core. Each Asio and CPU thread communicate exclusively with the other thread on the same core. This demonstrates CPU offloading capabilities while maintaining a "share-nothing" architecture, as the two threads will be SMT siblings, so they will be making use of the same L1 cache.
- asio_server_per_core_prefork.sh: Runs the prior, with each pair of executors in its own process, and each process pinned to a different core.

- asio_server_per_cache.cpp: Creates an Asio thread and a CPU thread pool for each processor cache. e.g. on Zen chiplet architecture, each chiplet has its own L3 cache, so this would create 1 working group per chiplet. Threads communicate exclusively with the other threads in the same cache. This allows for increased I/O scaling while also allowing for heavy CPU offload, for applications that need to balance I/O and compute performance on many-core machines.
- asio_server_per_core_prefork.sh: Runs the prior, with each pair of executors in its own process, and each process pinned to a different cache.
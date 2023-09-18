Runs the skynet benchmark repeatedly to let you compare the performance of different allocators and numbers of threads.
Outputs to stdout as well as `./results/{git_sha}.csv`.

With NTHREADS ranging from 1 to your machine's CPU core count.
For each, links the program against each of these allocator libraries and runs it:
- libtcmalloc
- libjemalloc
- libmimalloc
- (nothing)

Execute `./run_bench.sh` to run it.

If you get linker errors, you may need to edit `./run_bench_impl.sh` with the locations of the allocator libs on your machine.

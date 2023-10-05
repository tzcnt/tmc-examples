#!/bin/bash
nproc=$(cat /proc/cpuinfo | awk '($1 == "processor") {count++ } END { print count }')
if [ $nproc -gt 64 ]; then 
    nproc=64
fi
echo "NTHREADS,ptmalloc,tcmalloc,jemalloc,mimalloc"
for ((nthreads=1; nthreads<=nproc; nthreads++))
do
    echo -n "$nthreads,"
    echo "#define NTHREADS $nthreads" > ./build/bench_config.hpp
    clang++ -DTMC_PRIORITY_COUNT=1 -DTMC_USE_HWLOC -I../../../submodules/TooManyCooks/include -O3 -DNDEBUG -std=gnu++20 -march=native -c main.cpp -o ./build/main.cpp.o > /dev/null 2>&1

    clang++ ./build/main.cpp.o /usr/lib/libhwloc.so -o ./build/bench
    sleep 1
    ./build/bench
    echo -n ","

    clang++ ./build/main.cpp.o /usr/lib/libhwloc.so /usr/lib/libtcmalloc_minimal.so.4 -o ./build/bench
    sleep 1
    ./build/bench
    echo -n ","

    clang++ ./build/main.cpp.o /usr/lib/libhwloc.so /usr/lib/libjemalloc.so.2 -o ./build/bench
    sleep 1
    ./build/bench
    echo -n ","

    clang++ ./build/main.cpp.o /usr/lib/libhwloc.so /usr/lib/libmimalloc.so.2 -o ./build/bench
    sleep 1
    ./build/bench
    echo ""
done

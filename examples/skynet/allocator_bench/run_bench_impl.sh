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
    clang++ -DTMC_PRIORITY_COUNT=1 -I../../../include -O3 -DNDEBUG -std=gnu++20 -march=native -c main.cpp -o ./build/main.cpp.o > /dev/null 2>&1

    clang++ ./build/main.cpp.o -o ./build/bench
    sleep 1
    ./build/bench
    echo -n ","

    clang++ ./build/main.cpp.o /usr/lib/x86_64-linux-gnu/libtcmalloc_minimal.so.4 -o ./build/bench
    sleep 1
    ./build/bench
    echo -n ","

    clang++ ./build/main.cpp.o /usr/lib/x86_64-linux-gnu/libjemalloc.so.2 -o ./build/bench
    sleep 1
    ./build/bench
    echo -n ","

    clang++ ./build/main.cpp.o /usr/lib/x86_64-linux-gnu/libmimalloc.so.2 -o ./build/bench
    sleep 1
    ./build/bench
    echo ""
done

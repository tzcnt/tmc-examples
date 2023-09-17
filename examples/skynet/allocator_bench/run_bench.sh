#!/bin/bash
mkdir -p ./build
mkdir -p ./results
commit=$(git rev-parse --short HEAD)
./run_bench_impl.sh | tee "./results/${commit}.csv"

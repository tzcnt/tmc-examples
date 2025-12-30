#!/bin/bash
set -e

cd "$(dirname "$0")/.."

IMAGE_NAME="tmc-container-tests"

echo "=== Building container test image ==="
docker build -f ./tests/Dockerfile.test_container -t "$IMAGE_NAME" .

echo ""
echo "=== Test 1: Unlimited (no CPU constraints) ==="
docker run --rm \
    -e TMC_CONTAINER_TEST=unlimited \
    "$IMAGE_NAME" \
    ./test_container --gtest_filter='*unlimited*'

echo ""
echo "=== Test 2: CPU quota via --cpus (cgroups detection) ==="
docker run --rm \
    --cpus=1.5 \
    -e TMC_CONTAINER_TEST=cpu_quota \
    -e TMC_EXPECTED_CPUS=1.5 \
    "$IMAGE_NAME" \
    ./test_container --gtest_filter='*cpu_quota*'

echo ""
echo "=== Test 3: CPU set via --cpuset-cpus (hwloc detection) ==="
docker run --rm \
    --cpuset-cpus="0,1" \
    -e TMC_CONTAINER_TEST=cpuset \
    -e TMC_EXPECTED_CPUS=2 \
    "$IMAGE_NAME" \
    ./test_container --gtest_filter='*cpuset*'

echo ""
echo "=== All container tests passed ==="

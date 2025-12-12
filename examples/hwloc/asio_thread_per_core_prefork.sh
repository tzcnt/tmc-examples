#!/bin/bash
CMAKE_PRESET=clang-linux-release
SCRIPT_DIR="$( cd "$( dirname "$(readlink -f "${BASH_SOURCE[0]}")" )" && pwd )"
PROGRAM=$SCRIPT_DIR/../../build/$CMAKE_PRESET/hwloc_asio_thread_per_core
if ! [ -e "$PROGRAM" ]; then
  echo "$PROGRAM does not exist. Build the example first, or edit CMAKE_PRESET in this script."
  exit 1
fi

CORES=$($PROGRAM --query)

cleanup() {
  echo "Terminating child processes..."
  kill 0
  wait
  exit 0
}
trap cleanup SIGINT

echo "Detected $CORES cores. Forking a worker process per core..."
for (( i=0; i<$CORES; i++ )); do
  eval "$PROGRAM $i" &
done

wait

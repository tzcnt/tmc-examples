#!/bin/bash
CMAKE_PRESET=clang-linux-release
SCRIPT_DIR="$( cd "$( dirname "$(readlink -f "${BASH_SOURCE[0]}")" )" && pwd )"
PROGRAM=$SCRIPT_DIR/../../build/$CMAKE_PRESET/hwloc_asio_server_per_cache
if ! [ -e "$PROGRAM" ]; then
  echo "$PROGRAM does not exist. Build the example first, or edit CMAKE_PRESET in this script."
  exit 1
fi

CACHES=$($PROGRAM --query)

cleanup() {
  echo "Terminating child processes..."
  kill 0
  wait
  exit 0
}
trap cleanup SIGINT

echo "Detected $CACHES caches. Forking a worker process per cache..."
for (( i=0; i<$CACHES; i++ )); do
  eval "$PROGRAM $i" &
done

wait

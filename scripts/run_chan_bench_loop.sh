#!/usr/bin/env bash

set -uo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
BINARY=${BINARY:-"$ROOT_DIR/build/clang-linux-release/chan_bench"}
RUNS=${RUNS:-20}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT_DIR=${OUT_DIR:-"$ROOT_DIR/build/clang-linux-release/chan_bench-runs/$STAMP"}

mkdir -p "$OUT_DIR"

if [[ ! -x "$BINARY" ]]; then
  echo "missing executable: $BINARY" >&2
  exit 1
fi

echo "binary: $BINARY"
echo "runs:   $RUNS"
echo "logs:   $OUT_DIR"

failures=0

for run in $(seq 1 "$RUNS"); do
  log_file=$(printf '%s/run-%02d.log' "$OUT_DIR" "$run")
  echo "[$(date +%H:%M:%S)] run $run/$RUNS -> $log_file"

  ASAN_OPTIONS=${ASAN_OPTIONS:-abort_on_error=1:halt_on_error=1} \
    "$BINARY" >"$log_file" 2>&1
  status=$?

  if [[ $status -ne 0 ]]; then
    echo "run $run failed with status $status"
    failures=$((failures + 1))
  fi
done

echo
echo "failure summary: $failures failed runs"
echo "grep hints:"
echo "  rg -n 'ERROR: AddressSanitizer|ThreadSanitizer|Segmentation fault|FAIL:' '$OUT_DIR'"

if [[ $failures -ne 0 ]]; then
  exit 1
fi

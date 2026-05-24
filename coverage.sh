#!/usr/bin/env bash
# Generate a coverage report for the TooManyCooks library using llvm tooling.
#
# Usage:
#   ./coverage.sh                 # generate a coverage summary (lcov + report)
#   ./coverage.sh <FILE>          # also show line-by-line coverage for <FILE>
#                                 # (path relative to repo root or matching a
#                                 #  source path under ./submodules/TooManyCooks)
#
# Configuration via environment variables:
#   PRESET     (default: clang-linux-debug)
#   BUILD_DIR  (default: build/coverage-${PRESET})
#   WORK_ITEM  (default: CORO)
#   HWLOC      (default: ON)
#
# Requires:
#   llvm-profdata (any version), llvm-cov (any version), cmake, clang++.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PRESET="${PRESET:-clang-linux-debug}"
WORK_ITEM="${WORK_ITEM:-CORO}"
HWLOC="${HWLOC:-ON}"
BUILD_DIR="${BUILD_DIR:-build/coverage-${PRESET}}"

# ---------------------------------------------------------------------------
# Locate llvm tools. Prefer plain names, then the newest available numbered
# variant (matching the local installation, which may be newer than CI's).
# ---------------------------------------------------------------------------
find_llvm_tool() {
    local base="$1"
    if command -v "$base" >/dev/null 2>&1; then
        echo "$base"
        return 0
    fi
    # Look for numbered variants llvm-cov-21, llvm-cov-20, ...
    local found
    found=$(compgen -c "${base}-" 2>/dev/null \
        | grep -E "^${base}-[0-9]+$" \
        | sort -t- -k2,2 -n -r \
        | head -n1 || true)
    if [[ -n "$found" ]]; then
        echo "$found"
        return 0
    fi
    return 1
}

LLVM_PROFDATA=$(find_llvm_tool llvm-profdata) || {
    echo "ERROR: llvm-profdata not found. Install llvm tooling and try again." >&2
    exit 1
}
LLVM_COV=$(find_llvm_tool llvm-cov) || {
    echo "ERROR: llvm-cov not found. Install llvm tooling and try again." >&2
    exit 1
}
echo "Using: $LLVM_PROFDATA"
echo "Using: $LLVM_COV"

# ---------------------------------------------------------------------------
# Configure + build with coverage instrumentation.
# ---------------------------------------------------------------------------
COV_FLAGS="-fprofile-instr-generate;-fcoverage-mapping"
EXTRA_OPTS=()
if [[ "$WORK_ITEM" == "FUNC" ]]; then
    EXTRA_OPTS+=("-DTMC_TRIVIAL_TASK=ON")
fi

cmake \
    -DTMC_AS_SUBMODULE=ON \
    -DTMC_COPY_COMPILE_COMMANDS=OFF \
    -DTMC_USE_HWLOC="${HWLOC}" \
    -DTMC_WORK_ITEM="${WORK_ITEM}" \
    "${EXTRA_OPTS[@]}" \
    -DCMD_COMPILE_FLAGS="-Werror;${COV_FLAGS}" \
    -DCMD_LINK_FLAGS="-Wl,--build-id;${COV_FLAGS}" \
    -B "${BUILD_DIR}" \
    --preset "${PRESET}" \
    .

cmake --build "${BUILD_DIR}" \
    --parallel "$(nproc)" \
    --target tests test_exceptions test_fuzz_chan

# ---------------------------------------------------------------------------
# Run tests and merge raw profiles.
# ---------------------------------------------------------------------------
PROF_DIR="${BUILD_DIR}/coverage-profiles"
rm -rf "${PROF_DIR}"
mkdir -p "${PROF_DIR}"

LLVM_PROFILE_FILE="${PROF_DIR}/tests.profraw" \
    "${BUILD_DIR}/tests/tests"
LLVM_PROFILE_FILE="${PROF_DIR}/test_exceptions.profraw" \
    "${BUILD_DIR}/tests/test_exceptions"
LLVM_PROFILE_FILE="${PROF_DIR}/test_fuzz_chan.profraw" \
    "${BUILD_DIR}/tests/test_fuzz_chan"

"${LLVM_PROFDATA}" merge \
    -o "${PROF_DIR}/coverage.profdata" \
    "${PROF_DIR}/tests.profraw" \
    "${PROF_DIR}/test_exceptions.profraw" \
    "${PROF_DIR}/test_fuzz_chan.profraw"

# ---------------------------------------------------------------------------
# Emit reports. Always produce an lcov file scoped to TooManyCooks sources;
# optionally show line-by-line coverage for a specific file.
# ---------------------------------------------------------------------------
LCOV_FILE="${BUILD_DIR}/coverage.lcov"
"${LLVM_COV}" export \
    -format=lcov \
    -instr-profile "${PROF_DIR}/coverage.profdata" \
    -object "${BUILD_DIR}/tests/tests" \
    -object "${BUILD_DIR}/tests/test_exceptions" \
    -object "${BUILD_DIR}/tests/test_fuzz_chan" \
    -sources ./submodules/TooManyCooks/ \
    > "${LCOV_FILE}"
echo "Wrote lcov coverage to ${LCOV_FILE}"

echo
echo "===== Coverage summary (TooManyCooks sources) ====="
"${LLVM_COV}" report \
    -instr-profile "${PROF_DIR}/coverage.profdata" \
    -object "${BUILD_DIR}/tests/tests" \
    -object "${BUILD_DIR}/tests/test_exceptions" \
    -object "${BUILD_DIR}/tests/test_fuzz_chan" \
    -sources ./submodules/TooManyCooks/

if [[ $# -ge 1 ]]; then
    TARGET_FILE="$1"
    if [[ ! -f "${TARGET_FILE}" ]]; then
        # Try resolving relative to the TMC submodule include path.
        for candidate in \
            "submodules/TooManyCooks/${TARGET_FILE}" \
            "submodules/TooManyCooks/include/${TARGET_FILE}" \
            "submodules/TooManyCooks/include/tmc/${TARGET_FILE}"
        do
            if [[ -f "${candidate}" ]]; then
                TARGET_FILE="${candidate}"
                break
            fi
        done
    fi
    if [[ ! -f "${TARGET_FILE}" ]]; then
        echo "ERROR: source file '$1' not found." >&2
        exit 1
    fi

    ABS_TARGET="$(realpath "${TARGET_FILE}")"
    OUT_FILE="${BUILD_DIR}/coverage-$(basename "${TARGET_FILE}").txt"

    echo
    echo "===== Line-by-line coverage for ${TARGET_FILE} ====="
    "${LLVM_COV}" show \
        -instr-profile "${PROF_DIR}/coverage.profdata" \
        -object "${BUILD_DIR}/tests/tests" \
        -object "${BUILD_DIR}/tests/test_exceptions" \
        -object "${BUILD_DIR}/tests/test_fuzz_chan" \
        -show-line-counts-or-regions \
        -use-color=false \
        -sources "${ABS_TARGET}" \
        > "${OUT_FILE}"
    cat "${OUT_FILE}"
    echo
    echo "Wrote line-by-line coverage to ${OUT_FILE}"
fi

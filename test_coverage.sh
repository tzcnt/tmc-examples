#!/bin/bash
set -euo

# This script is designed to extract test coverage information for Github Actions that can be fed to codecov.io
# It builds and runs the test suites multiple times across all supported work item types,
# then combines the coverage information into a single file using a format that codecov.io supports.

# It can be run locally with the following preconditions:
# - TMC is checked out as a submodule in ./submodules/TooManyCooks
# - you have clang, llvm-profdata and llvm-cov installed
# - your build/clang-linux-debug folder is clear (it will be overwritten by this using Unix Makefiles generator)

# Output will be in ./coverage_data/coverage.txt

# matches what's on the Github Actions runner
# change this to run using your local machine LLVM version
llvm_version="18"

preset="clang-linux-debug"
build="./build/${preset}"
cov="./coverage_data"

mkdir -p ./${cov}

work_item_names=("CORO" "FUNCORO" "FUNC")
test_names=("tests" "test_exceptions" "test_fuzz_chan")

programs=""
raws="" 
for work_item in "${work_item_names[@]}"; do
  compile_flags="-DTMC_WORK_ITEM=${work_item};-fprofile-instr-generate;-fcoverage-mapping"
  if [ "${work_item}" == "FUNC" ]; then
    compile_flags+=";-DTMC_TRIVIAL_TASK"
  fi
  compile_flags="'${compile_flags}'"

  cmake -G "Unix Makefiles" -DTMC_AS_SUBMODULE=ON -DCMD_COMPILE_FLAGS=${compile_flags} -DCMD_LINK_FLAGS='-fprofile-instr-generate;-fcoverage-mapping' --preset ${preset} .
  cmake --build ${build} --parallel $(nproc) --target all

  for test in "${test_names[@]}"; do
    test_out_loc="${cov}/${work_item}-${test}.profraw"
    raws+=" ${test_out_loc}"
    LLVM_PROFILE_FILE=${test_out_loc} ${build}/tests/${test}
    
    exe_out_loc="${cov}/${work_item}-${test}.exe"
    programs+=" ${exe_out_loc}"
    mv ${build}/tests/${test} ${exe_out_loc}
  done
done

echo "Merging coverage data..."
"llvm-profdata-${llvm_version}" merge -sparse ${raws} -o ${cov}/coverage.profdata

echo "Generating coverage.txt..."
"llvm-cov-${llvm_version}" export -format=lcov -instr-profile=${cov}/coverage.profdata ${programs} ./submodules/TooManyCooks/ > ${cov}/coverage.txt

echo "Done."

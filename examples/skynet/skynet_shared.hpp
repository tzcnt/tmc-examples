#pragma once
#include <cstddef>

// The proper sum of skynet (1M tasks) is 499999500000.
// 32-bit platforms can't hold the full sum, but unsigned integer overflow is
// defined so it will wrap to this number.
static constexpr inline size_t EXPECTED_RESULT =
  sizeof(size_t) == 8 ? 499999500000 : 1783293664;

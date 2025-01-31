#pragma once

#include "tmc/all_headers.hpp" // IWYU pragma: export
#include "tmc/utils.hpp"       // IWYU pragma: export

static inline tmc::task<void> empty_task() { co_return; }

static inline tmc::task<int> int_task() { co_return 1; }

static inline void empty_func() {}

static inline int int_func() { return 1; }
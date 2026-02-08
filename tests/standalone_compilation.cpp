// The portions of TMC that require standalone compilation will be compiled in
// this translation unit due to the presence of the TMC_IMPL macro.
// Other translation units need only include the headers.

#define TMC_IMPL
#include "tmc/all_headers.hpp" // IWYU pragma: keep

// If building with TMC_WINDOWS_DLL then this impl must also be provided
#include "tmc/asio/ex_asio.hpp" // IWYU pragma: keep

// There are 3 ways to build and link to TMC:
// (nothing defined): everything is "inline" and the library is header-only.
// This file is not required.
//
// TMC_STANDALONE_COMPILATION is defined: The portions of TMC that require
// standalone compilation will be compiled in this translation unit due to the
// presence of the TMC_IMPL macro. Other translation units should also define
// TMC_STANDALONE_COMPILATION, but should not define TMC_IMPL.
//
// TMC_WINDOWS_DLL is defined: Behaves similarly to TMC_STANDALONE_COMPILATION,
// except that functions and data in this compilation unit will be decorated
// with "__declspec(dllexport)", so that you can build this file into a DLL.
// Other translation units should also define TMC_WINDOWS_DLL, but should not
// define TMC_IMPL. In those translation units, the headers will decorate the
// necessary functions and data with "__declspec(dllimport)" so that they can be
// linked to this DLL.

#define TMC_IMPL
#include "tmc/all_headers.hpp" // IWYU pragma: keep

// If building with TMC_WINDOWS_DLL, and you want to use Asio, then this impl
// must also be provided. If you don't need Asio, or you are just using
// TMC_STANDALONE_COMPILATION, then this is not required.
#ifdef TMC_WINDOWS_DLL
#include "tmc/asio/ex_asio.hpp" // IWYU pragma: keep
#endif

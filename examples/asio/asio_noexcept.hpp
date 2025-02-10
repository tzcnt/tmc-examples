// In order to build with -fno-exceptions and -DASIO_NO_EXCEPTIONS, this
// function must be defined. Asio will call this function when it would throw an
// exception.

// For now, TooManyCooks does not support exceptions, so this is the right thing
// to do.

#pragma once

#include <cstdio>
#include <exception>

namespace asio::detail {
template <typename Exception> void throw_exception(const Exception& e) {
  std::printf("Asio threw exception: %s", e.what());
  std::terminate();
}
}; // namespace asio::detail
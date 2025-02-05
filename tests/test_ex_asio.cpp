#include "test_common.hpp"
#include "tmc/asio/ex_asio.hpp"

#include <gtest/gtest.h>
#include <ranges>

#define CATEGORY test_ex_asio

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() { tmc::asio_executor().init(); }

  static void TearDownTestSuite() { tmc::asio_executor().teardown(); }

  static tmc::ex_asio& ex() { return tmc::asio_executor(); }
};

#include "test_executors.ipp"

#undef CATEGORY
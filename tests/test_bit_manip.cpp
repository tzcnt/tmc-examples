#include "tmc/detail/bit_manip.hpp"
#include "tmc/detail/compat.hpp"

#include <gtest/gtest.h>

#include <atomic>

#define CATEGORY test_bit_manip

class CATEGORY : public testing::Test {};

TEST_F(CATEGORY, atomic_bit_reset_test_true) {
  std::atomic<size_t> v = 2;
  auto set = tmc::detail::atomic_bit_reset_test(v, 1);
  EXPECT_TRUE(set);
  EXPECT_EQ(v.load(), 0);
}

TEST_F(CATEGORY, atomic_bit_reset_test_false) {
  std::atomic<size_t> v = 2;
  auto set = tmc::detail::atomic_bit_reset_test(v, 3);
  EXPECT_FALSE(set);
  EXPECT_EQ(v.load(), 2);
}

TEST_F(CATEGORY, atomic_bit_reset_true) {
  std::atomic<size_t> v = 2;
  tmc::detail::atomic_bit_reset(v, 1);
  EXPECT_EQ(v.load(), 0);
}

TEST_F(CATEGORY, atomic_bit_reset_false) {
  std::atomic<size_t> v = 2;
  tmc::detail::atomic_bit_reset(v, 3);
  EXPECT_EQ(v.load(), 2);
}

TEST_F(CATEGORY, atomic_bit_set_test_false) {
  std::atomic<size_t> v = 2;
  auto set = tmc::detail::atomic_bit_set_test(v, 3);
  EXPECT_FALSE(set);
  EXPECT_EQ(v.load(), 10);
}

TEST_F(CATEGORY, atomic_bit_set_test_true) {
  std::atomic<size_t> v = 2;
  auto set = tmc::detail::atomic_bit_set_test(v, 1);
  EXPECT_TRUE(set);
  EXPECT_EQ(v.load(), 2);
}

TEST_F(CATEGORY, atomic_bit_set_false) {
  std::atomic<size_t> v = 2;
  tmc::detail::atomic_bit_set_test(v, 3);
  EXPECT_EQ(v.load(), 10);
}

TEST_F(CATEGORY, atomic_bit_set_true) {
  std::atomic<size_t> v = 2;
  tmc::detail::atomic_bit_set_test(v, 1);
  EXPECT_EQ(v.load(), 2);
}

TEST_F(CATEGORY, blsi) {
  EXPECT_EQ(tmc::detail::blsi(0), 0);
  EXPECT_EQ(tmc::detail::blsi(1), 1);
  EXPECT_EQ(tmc::detail::blsi(2), 2);
  EXPECT_EQ(tmc::detail::blsi(3), 1);
  EXPECT_EQ(
    tmc::detail::blsi(TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)),
    TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)
  );
  EXPECT_EQ(
    tmc::detail::blsi(
      (TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)) |
      (TMC_ONE_BIT << (TMC_PLATFORM_BITS - 2))
    ),
    TMC_ONE_BIT << (TMC_PLATFORM_BITS - 2)
  );
  EXPECT_EQ(tmc::detail::blsi((TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)) | 1), 1);
}

TEST_F(CATEGORY, blsr) {
  EXPECT_EQ(tmc::detail::blsr(0), 0);
  EXPECT_EQ(tmc::detail::blsr(1), 0);
  EXPECT_EQ(tmc::detail::blsr(2), 0);
  EXPECT_EQ(tmc::detail::blsr(3), 2);
  EXPECT_EQ(tmc::detail::blsr(TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)), 0);
  EXPECT_EQ(
    tmc::detail::blsr(
      (TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)) |
      (TMC_ONE_BIT << (TMC_PLATFORM_BITS - 2))
    ),
    TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)
  );
  EXPECT_EQ(
    tmc::detail::blsr((TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)) | 1),
    TMC_ONE_BIT << (TMC_PLATFORM_BITS - 1)
  );
}

// These delegate to stdlib so no need to test extensively
TEST_F(CATEGORY, tzcnt) { EXPECT_EQ(tmc::detail::tzcnt(1), 0); }

TEST_F(CATEGORY, lzcnt) { EXPECT_EQ(tmc::detail::lzcnt(1), 63); }

TEST_F(CATEGORY, popcnt) { EXPECT_EQ(tmc::detail::popcnt(32), 1); }

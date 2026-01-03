#include "test_common.hpp"
#include "tmc/detail/atomic_bitmap.hpp"

#include <gtest/gtest.h>

#ifdef TMC_MORE_THREADS

#define CATEGORY test_atomic_bitmap

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, atomic_bitmap_default_constructor) {
  tmc::detail::atomic_bitmap ab;
  EXPECT_EQ(ab.get_word_count(), 1u);
}

TEST_F(CATEGORY, atomic_bitmap_init_and_clear) {
  tmc::detail::atomic_bitmap ab;

  ab.init(64);
  EXPECT_EQ(ab.get_word_count(), 1u);

  ab.clear();
  EXPECT_EQ(ab.get_word_count(), 1u);

  // Reinit after clear
  ab.init(128);
  EXPECT_EQ(ab.get_word_count(), 2u);
}

TEST_F(CATEGORY, atomic_bitmap_init_various_sizes) {
  tmc::detail::atomic_bitmap ab;

  // Single bit
  ab.init(1);
  EXPECT_EQ(ab.get_word_count(), 1u);
  ab.clear();

  // Exactly one word
  ab.init(64);
  EXPECT_EQ(ab.get_word_count(), 1u);
  ab.clear();

  // Just over one word
  ab.init(65);
  EXPECT_EQ(ab.get_word_count(), 2u);
  ab.clear();

  // Multiple words
  ab.init(200);
  EXPECT_EQ(ab.get_word_count(), 4u);
}

TEST_F(CATEGORY, atomic_bitmap_set_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));

  ab.set_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(ab.load_word(0), 1u);
  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));

  ab.set_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(ab.load_word(0), 1u);
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  // Set a bit first
  ab.set_bit(5, std::memory_order_relaxed);
  EXPECT_TRUE(ab.test_bit(5, std::memory_order_relaxed));

  ab.clr_bit(5, std::memory_order_relaxed);
  EXPECT_EQ(ab.load_word(0), 0u);
  EXPECT_FALSE(ab.test_bit(5, std::memory_order_relaxed));

  // Clearing already-clear bit
  ab.clr_bit(5, std::memory_order_relaxed);
  EXPECT_EQ(ab.load_word(0), 0u);
}

TEST_F(CATEGORY, atomic_bitmap_test_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  // Test bits across word boundaries
  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(63, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(64, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(127, std::memory_order_relaxed));

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(63, std::memory_order_relaxed);
  ab.set_bit(64, std::memory_order_relaxed);
  ab.set_bit(127, std::memory_order_relaxed);

  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(63, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(64, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(127, std::memory_order_relaxed));
}

TEST_F(CATEGORY, atomic_bitmap_load_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(1, std::memory_order_relaxed);
  ab.set_bit(64, std::memory_order_relaxed);

  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), 3u);
  EXPECT_EQ(ab.load_word(1, std::memory_order_relaxed), 1u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_single_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  EXPECT_EQ(ab.popcnt(), 0u);

  ab.set_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(ab.popcnt(), 1u);

  ab.set_bit(63, std::memory_order_relaxed);
  EXPECT_EQ(ab.popcnt(), 2u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_partial_word) {
  // Test popcnt with non-word-aligned bit count (tests valid_mask_for_word)
  tmc::detail::atomic_bitmap ab;
  ab.init(10); // Only 10 bits, partial word

  for (size_t i = 0; i < 10; ++i) {
    ab.set_bit(i, std::memory_order_relaxed);
  }

  EXPECT_EQ(ab.popcnt(), 10u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_multi_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(63, std::memory_order_relaxed);
  ab.set_bit(64, std::memory_order_relaxed);
  ab.set_bit(127, std::memory_order_relaxed);

  EXPECT_EQ(ab.popcnt(), 4u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_or) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(64);
  ab2.init(64);

  ab1.set_bit(0, std::memory_order_relaxed);
  ab1.set_bit(2, std::memory_order_relaxed);
  ab2.set_bit(1, std::memory_order_relaxed);
  ab2.set_bit(2, std::memory_order_relaxed);

  // Union: bits 0, 1, 2 (bit 2 is in both)
  EXPECT_EQ(ab1.popcnt_or(ab2, std::memory_order_relaxed), 3u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_or_multi_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(128);
  ab2.init(128);

  ab1.set_bit(0, std::memory_order_relaxed);
  ab1.set_bit(64, std::memory_order_relaxed);
  ab2.set_bit(63, std::memory_order_relaxed);
  ab2.set_bit(127, std::memory_order_relaxed);

  EXPECT_EQ(ab1.popcnt_or(ab2, std::memory_order_relaxed), 4u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_or_partial_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(70); // Partial second word
  ab2.init(70);

  ab1.set_bit(0, std::memory_order_relaxed);
  ab1.set_bit(69, std::memory_order_relaxed);
  ab2.set_bit(1, std::memory_order_relaxed);
  ab2.set_bit(68, std::memory_order_relaxed);

  EXPECT_EQ(ab1.popcnt_or(ab2, std::memory_order_relaxed), 4u);
}

TEST_F(CATEGORY, atomic_bitmap_load_or) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(64);
  ab2.init(64);

  ab1.set_bit(0, std::memory_order_relaxed);
  ab2.set_bit(1, std::memory_order_relaxed);

  size_t combined = ab1.load_or(ab2, 0, std::memory_order_relaxed);
  EXPECT_EQ(combined, 3u); // bits 0 and 1
}

TEST_F(CATEGORY, atomic_bitmap_load_inverted_or) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(8); // 8 bits
  ab2.init(8);

  ab1.set_bit(0, std::memory_order_relaxed);
  ab1.set_bit(1, std::memory_order_relaxed);
  ab2.set_bit(2, std::memory_order_relaxed);
  ab2.set_bit(3, std::memory_order_relaxed);

  // Inverted OR: ~(0b1111) & 0xFF = 0b11110000
  size_t result = ab1.load_inverted_or(ab2, 0, std::memory_order_relaxed);
  EXPECT_EQ(result, 0xF0u);
}

TEST_F(CATEGORY, atomic_bitmap_load_inverted_or_full_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(64);
  ab2.init(64);

  ab1.set_bit(0, std::memory_order_relaxed);

  size_t result = ab1.load_inverted_or(ab2, 0, std::memory_order_relaxed);
  EXPECT_EQ(result, ~size_t(1));
}

TEST_F(CATEGORY, atomic_bitmap_find_first_set_bit_empty) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  size_t bit_out = 999;
  EXPECT_FALSE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 999u); // unchanged
}

TEST_F(CATEGORY, atomic_bitmap_find_first_set_bit_single) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  ab.set_bit(42, std::memory_order_relaxed);

  size_t bit_out = 0;
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 42u);
}

TEST_F(CATEGORY, atomic_bitmap_find_first_set_bit_multi_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  // Set bit in second word only
  ab.set_bit(100, std::memory_order_relaxed);

  size_t bit_out = 0;
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 100u);

  // Now set a bit in first word
  ab.set_bit(10, std::memory_order_relaxed);
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 10u);
}

TEST_F(CATEGORY, atomic_bitmap_find_first_set_bit_at_zero) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  ab.set_bit(0, std::memory_order_relaxed);

  size_t bit_out = 999;
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 0u);
}

TEST_F(CATEGORY, popcnt_and) {
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(8);
  mask.init(8);

  // Set bits 0, 2, 4, 6 in atomic_bitmap
  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(2, std::memory_order_relaxed);
  ab.set_bit(4, std::memory_order_relaxed);
  ab.set_bit(6, std::memory_order_relaxed);

  // Mask allows only bits 0, 1, 2, 3 (first half)
  mask.set_bit(0);
  mask.set_bit(1);
  mask.set_bit(2);
  mask.set_bit(3);

  // Without mask: 4 bits set (0, 2, 4, 6)
  EXPECT_EQ(ab.popcnt(), 4u);

  // With mask: only bits 0 and 2 are both set and allowed
  EXPECT_EQ(ab.popcnt_and(mask, std::memory_order_relaxed), 2u);
}

TEST_F(CATEGORY, popcnt_and_multi_word) {
  // Test popcnt_and with more than 64 bits (multi-word bitmap)
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(128);
  mask.init(128);

  // Set bits in first word (0-63) and second word (64-127)
  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(63, std::memory_order_relaxed);
  ab.set_bit(64, std::memory_order_relaxed);
  ab.set_bit(127, std::memory_order_relaxed);

  // Mask allows only the first word (bits 0-63)
  for (size_t i = 0; i < 64; ++i) {
    mask.set_bit(i);
  }

  EXPECT_EQ(ab.popcnt(), 4u);
  EXPECT_EQ(ab.popcnt_and(mask, std::memory_order_relaxed), 2u);
}

TEST_F(CATEGORY, popcnt_and_empty_mask) {
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(64);
  mask.init(64);

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(32, std::memory_order_relaxed);
  ab.set_bit(63, std::memory_order_relaxed);

  // Empty mask - no bits allowed
  EXPECT_EQ(ab.popcnt(), 3u);
  EXPECT_EQ(ab.popcnt_and(mask, std::memory_order_relaxed), 0u);
}

TEST_F(CATEGORY, popcnt_and_partial_word) {
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(70); // Partial second word
  mask.init(70);

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(65, std::memory_order_relaxed);
  ab.set_bit(69, std::memory_order_relaxed);

  mask.set_bit(0);
  mask.set_bit(69);

  EXPECT_EQ(ab.popcnt(), 3u);
  EXPECT_EQ(ab.popcnt_and(mask, std::memory_order_relaxed), 2u);
}

TEST_F(CATEGORY, bitmap_default_constructor) {
  tmc::detail::bitmap bm;
  // Just verify it doesn't crash - no public way to check word count
  bm.init(64);
  bm.clear();
}

TEST_F(CATEGORY, bitmap_init_and_clear) {
  tmc::detail::bitmap bm;

  bm.init(64);
  bm.set_bit(0);
  EXPECT_TRUE(bm.test_bit(0));

  bm.clear();

  // Reinit after clear
  bm.init(128);
  EXPECT_FALSE(bm.test_bit(0));
}

TEST_F(CATEGORY, bitmap_move_constructor) {
  tmc::detail::bitmap bm1;
  bm1.init(64);
  bm1.set_bit(5);
  bm1.set_bit(10);

  tmc::detail::bitmap bm2(std::move(bm1));

  EXPECT_TRUE(bm2.test_bit(5));
  EXPECT_TRUE(bm2.test_bit(10));
  EXPECT_FALSE(bm2.test_bit(0));
}

TEST_F(CATEGORY, bitmap_move_assignment) {
  tmc::detail::bitmap bm1;
  bm1.init(64);
  bm1.set_bit(15);

  tmc::detail::bitmap bm2;
  bm2.init(32);
  bm2.set_bit(0);

  bm2 = std::move(bm1);

  EXPECT_TRUE(bm2.test_bit(15));
  EXPECT_FALSE(bm2.test_bit(0));
}

TEST_F(CATEGORY, bitmap_move_assignment_self) {
  tmc::detail::bitmap bm;
  bm.init(64);
  bm.set_bit(20);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wself-move"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wself-move"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 26800)
#endif
  bm = std::move(bm);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

  EXPECT_TRUE(bm.test_bit(20));
}

TEST_F(CATEGORY, bitmap_set_and_test_bit) {
  tmc::detail::bitmap bm;
  bm.init(128);

  EXPECT_FALSE(bm.test_bit(0));
  EXPECT_FALSE(bm.test_bit(63));
  EXPECT_FALSE(bm.test_bit(64));
  EXPECT_FALSE(bm.test_bit(127));

  bm.set_bit(0);
  bm.set_bit(63);
  bm.set_bit(64);
  bm.set_bit(127);

  EXPECT_TRUE(bm.test_bit(0));
  EXPECT_TRUE(bm.test_bit(63));
  EXPECT_TRUE(bm.test_bit(64));
  EXPECT_TRUE(bm.test_bit(127));

  // Non-set bits remain false
  EXPECT_FALSE(bm.test_bit(1));
  EXPECT_FALSE(bm.test_bit(62));
  EXPECT_FALSE(bm.test_bit(65));
}

TEST_F(CATEGORY, bitmap_set_bit_idempotent) {
  tmc::detail::bitmap bm;
  bm.init(64);

  bm.set_bit(10);
  EXPECT_TRUE(bm.test_bit(10));

  bm.set_bit(10); // Set again
  EXPECT_TRUE(bm.test_bit(10));
}

TEST_F(CATEGORY, bitmap_load_word) {
  tmc::detail::bitmap bm;
  bm.init(128);

  bm.set_bit(0);
  bm.set_bit(1);
  bm.set_bit(64);

  EXPECT_EQ(bm.load_word(0), 3u);
  EXPECT_EQ(bm.load_word(1), 1u);
}

TEST_F(CATEGORY, bitmap_various_sizes) {
  tmc::detail::bitmap bm;

  // Single bit
  bm.init(1);
  bm.set_bit(0);
  EXPECT_TRUE(bm.test_bit(0));
  bm.clear();

  // Partial word
  bm.init(10);
  bm.set_bit(9);
  EXPECT_TRUE(bm.test_bit(9));
  EXPECT_FALSE(bm.test_bit(0));
  bm.clear();

  // Exactly one word
  bm.init(64);
  bm.set_bit(63);
  EXPECT_TRUE(bm.test_bit(63));
  bm.clear();

  // Just over one word
  bm.init(65);
  bm.set_bit(64);
  EXPECT_TRUE(bm.test_bit(64));
}

TEST_F(CATEGORY, atomic_bitmap_set_bit_popcnt) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  EXPECT_EQ(ab.set_bit_popcnt(0), 1u);
  EXPECT_EQ(ab.set_bit_popcnt(63), 2u);
  EXPECT_EQ(ab.set_bit_popcnt(64), 3u);
  EXPECT_EQ(ab.set_bit_popcnt(127), 4u);

  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(63, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(64, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(127, std::memory_order_relaxed));
}

TEST_F(CATEGORY, atomic_bitmap_set_bit_popcnt_partial_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(70);

  EXPECT_EQ(ab.set_bit_popcnt(0), 1u);
  EXPECT_EQ(ab.set_bit_popcnt(69), 2u);
  EXPECT_EQ(ab.popcnt(), 2u);
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit_popcnt) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  ab.set_bit(0);
  ab.set_bit(63);
  ab.set_bit(64);
  ab.set_bit(127);

  EXPECT_EQ(ab.clr_bit_popcnt(127), 3u);
  EXPECT_EQ(ab.clr_bit_popcnt(64), 2u);
  EXPECT_EQ(ab.clr_bit_popcnt(63), 1u);
  EXPECT_EQ(ab.clr_bit_popcnt(0), 0u);

  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(63, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(64, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(127, std::memory_order_relaxed));
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit_popcnt_partial_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(70);

  ab.set_bit(0);
  ab.set_bit(69);

  EXPECT_EQ(ab.clr_bit_popcnt(69), 1u);
  EXPECT_EQ(ab.clr_bit_popcnt(0), 0u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_or_and) {
  tmc::detail::atomic_bitmap ab1, ab2;
  tmc::detail::bitmap mask;
  ab1.init(64);
  ab2.init(64);
  mask.init(64);

  ab1.set_bit(0);
  ab1.set_bit(2);
  ab2.set_bit(1);
  ab2.set_bit(3);

  mask.set_bit(0);
  mask.set_bit(1);
  mask.set_bit(2);

  // Union of ab1 and ab2: bits 0,1,2,3
  // AND with mask (bits 0,1,2): bits 0,1,2
  EXPECT_EQ(ab1.popcnt_or_and(ab2, mask, std::memory_order_relaxed), 3u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_or_and_multi_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  tmc::detail::bitmap mask;
  ab1.init(128);
  ab2.init(128);
  mask.init(128);

  ab1.set_bit(0);
  ab1.set_bit(64);
  ab2.set_bit(63);
  ab2.set_bit(127);

  for (size_t i = 0; i < 64; ++i) {
    mask.set_bit(i);
  }

  // Union: 0,63,64,127; mask allows 0-63 only
  EXPECT_EQ(ab1.popcnt_or_and(ab2, mask, std::memory_order_relaxed), 2u);
}

TEST_F(CATEGORY, atomic_bitmap_popcnt_or_and_partial_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  tmc::detail::bitmap mask;
  ab1.init(70);
  ab2.init(70);
  mask.init(70);

  ab1.set_bit(0);
  ab1.set_bit(69);
  ab2.set_bit(1);
  ab2.set_bit(68);

  mask.set_bit(0);
  mask.set_bit(1);
  mask.set_bit(69);

  // Union: 0,1,68,69; mask allows 0,1,69
  EXPECT_EQ(ab1.popcnt_or_and(ab2, mask, std::memory_order_relaxed), 3u);
}

TEST_F(CATEGORY, bitmap_get_word_count) {
  tmc::detail::bitmap bm;

  bm.init(1);
  EXPECT_EQ(bm.get_word_count(), 1u);
  bm.clear();

  bm.init(64);
  EXPECT_EQ(bm.get_word_count(), 1u);
  bm.clear();

  bm.init(65);
  EXPECT_EQ(bm.get_word_count(), 2u);
  bm.clear();

  bm.init(128);
  EXPECT_EQ(bm.get_word_count(), 2u);
  bm.clear();

  bm.init(200);
  EXPECT_EQ(bm.get_word_count(), 4u);
}

TEST_F(CATEGORY, bitmap_valid_mask_for_word) {
  tmc::detail::bitmap bm;

  bm.init(64);
  EXPECT_EQ(bm.valid_mask_for_word(0), ~size_t(0));
  bm.clear();

  bm.init(128);
  EXPECT_EQ(bm.valid_mask_for_word(0), ~size_t(0));
  EXPECT_EQ(bm.valid_mask_for_word(1), ~size_t(0));
  bm.clear();

  bm.init(10);
  EXPECT_EQ(bm.valid_mask_for_word(0), (size_t(1) << 10) - 1);
  bm.clear();

  bm.init(70);
  EXPECT_EQ(bm.valid_mask_for_word(0), ~size_t(0));
  EXPECT_EQ(bm.valid_mask_for_word(1), (size_t(1) << 6) - 1);
}

TEST_F(CATEGORY, bitmap_popcnt) {
  tmc::detail::bitmap bm;
  bm.init(128);

  EXPECT_EQ(bm.popcnt(), 0u);

  bm.set_bit(0);
  EXPECT_EQ(bm.popcnt(), 1u);

  bm.set_bit(63);
  EXPECT_EQ(bm.popcnt(), 2u);

  bm.set_bit(64);
  EXPECT_EQ(bm.popcnt(), 3u);

  bm.set_bit(127);
  EXPECT_EQ(bm.popcnt(), 4u);
}

TEST_F(CATEGORY, bitmap_popcnt_partial_word) {
  tmc::detail::bitmap bm;
  bm.init(10);

  for (size_t i = 0; i < 10; ++i) {
    bm.set_bit(i);
  }

  EXPECT_EQ(bm.popcnt(), 10u);
}

#undef CATEGORY

#endif // TMC_MORE_THREADS

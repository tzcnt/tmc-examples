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

TEST_F(CATEGORY, atomic_bitmap_fetch_or_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  // Initially all bits are 0
  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));

  // fetch_or returns the previous value of the word
  size_t prev = ab.fetch_or_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(prev, 0u);
  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));

  // Setting the same bit again returns word with bit already set
  prev = ab.fetch_or_bit(0, std::memory_order_relaxed);
  EXPECT_NE(prev & 1u, 0u);
}

TEST_F(CATEGORY, atomic_bitmap_fetch_and_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  // Set a bit first
  ab.fetch_or_bit(5, std::memory_order_relaxed);
  EXPECT_TRUE(ab.test_bit(5, std::memory_order_relaxed));

  // fetch_and returns the previous value of the word
  size_t prev = ab.fetch_and_bit(5, std::memory_order_relaxed);
  EXPECT_NE(prev & (1ull << 5), 0u);
  EXPECT_FALSE(ab.test_bit(5, std::memory_order_relaxed));

  // Clearing already-clear bit
  prev = ab.fetch_and_bit(5, std::memory_order_relaxed);
  EXPECT_EQ(prev & (1ull << 5), 0u);
}

TEST_F(CATEGORY, atomic_bitmap_test_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  // Test bits across word boundaries
  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(63, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(64, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(127, std::memory_order_relaxed));

  ab.fetch_or_bit(0, std::memory_order_relaxed);
  ab.fetch_or_bit(63, std::memory_order_relaxed);
  ab.fetch_or_bit(64, std::memory_order_relaxed);
  ab.fetch_or_bit(127, std::memory_order_relaxed);

  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(63, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(64, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(127, std::memory_order_relaxed));
}

TEST_F(CATEGORY, atomic_bitmap_load_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  ab.fetch_or_bit(0, std::memory_order_relaxed);
  ab.fetch_or_bit(1, std::memory_order_relaxed);
  ab.fetch_or_bit(64, std::memory_order_relaxed);

  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), 3u);
  EXPECT_EQ(ab.load_word(1, std::memory_order_relaxed), 1u);
}

TEST_F(CATEGORY, atomic_bitmap_popcount_single_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 0u);

  ab.fetch_or_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 1u);

  ab.fetch_or_bit(63, std::memory_order_relaxed);
  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 2u);
}

TEST_F(CATEGORY, atomic_bitmap_popcount_partial_word) {
  // Test popcount with non-word-aligned bit count (tests valid_mask_for_word)
  tmc::detail::atomic_bitmap ab;
  ab.init(10); // Only 10 bits, partial word

  for (size_t i = 0; i < 10; ++i) {
    ab.fetch_or_bit(i, std::memory_order_relaxed);
  }

  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 10u);
}

TEST_F(CATEGORY, atomic_bitmap_popcount_multi_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  ab.fetch_or_bit(0, std::memory_order_relaxed);
  ab.fetch_or_bit(63, std::memory_order_relaxed);
  ab.fetch_or_bit(64, std::memory_order_relaxed);
  ab.fetch_or_bit(127, std::memory_order_relaxed);

  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 4u);
}

TEST_F(CATEGORY, atomic_bitmap_popcount_or) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(64);
  ab2.init(64);

  ab1.fetch_or_bit(0, std::memory_order_relaxed);
  ab1.fetch_or_bit(2, std::memory_order_relaxed);
  ab2.fetch_or_bit(1, std::memory_order_relaxed);
  ab2.fetch_or_bit(2, std::memory_order_relaxed);

  // Union: bits 0, 1, 2 (bit 2 is in both)
  EXPECT_EQ(ab1.popcount_or(ab2, std::memory_order_relaxed), 3u);
}

TEST_F(CATEGORY, atomic_bitmap_popcount_or_multi_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(128);
  ab2.init(128);

  ab1.fetch_or_bit(0, std::memory_order_relaxed);
  ab1.fetch_or_bit(64, std::memory_order_relaxed);
  ab2.fetch_or_bit(63, std::memory_order_relaxed);
  ab2.fetch_or_bit(127, std::memory_order_relaxed);

  EXPECT_EQ(ab1.popcount_or(ab2, std::memory_order_relaxed), 4u);
}

TEST_F(CATEGORY, atomic_bitmap_popcount_or_partial_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(70); // Partial second word
  ab2.init(70);

  ab1.fetch_or_bit(0, std::memory_order_relaxed);
  ab1.fetch_or_bit(69, std::memory_order_relaxed);
  ab2.fetch_or_bit(1, std::memory_order_relaxed);
  ab2.fetch_or_bit(68, std::memory_order_relaxed);

  EXPECT_EQ(ab1.popcount_or(ab2, std::memory_order_relaxed), 4u);
}

TEST_F(CATEGORY, atomic_bitmap_load_or) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(64);
  ab2.init(64);

  ab1.fetch_or_bit(0, std::memory_order_relaxed);
  ab2.fetch_or_bit(1, std::memory_order_relaxed);

  size_t combined = ab1.load_or(ab2, 0, std::memory_order_relaxed);
  EXPECT_EQ(combined, 3u); // bits 0 and 1
}

TEST_F(CATEGORY, atomic_bitmap_load_inverted_or) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(8); // 8 bits
  ab2.init(8);

  ab1.fetch_or_bit(0, std::memory_order_relaxed);
  ab1.fetch_or_bit(1, std::memory_order_relaxed);
  ab2.fetch_or_bit(2, std::memory_order_relaxed);
  ab2.fetch_or_bit(3, std::memory_order_relaxed);

  // Inverted OR: ~(0b1111) & 0xFF = 0b11110000
  size_t result = ab1.load_inverted_or(ab2, 0, std::memory_order_relaxed);
  EXPECT_EQ(result, 0xF0u);
}

TEST_F(CATEGORY, atomic_bitmap_load_inverted_or_full_word) {
  tmc::detail::atomic_bitmap ab1, ab2;
  ab1.init(64);
  ab2.init(64);

  ab1.fetch_or_bit(0, std::memory_order_relaxed);

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

  ab.fetch_or_bit(42, std::memory_order_relaxed);

  size_t bit_out = 0;
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 42u);
}

TEST_F(CATEGORY, atomic_bitmap_find_first_set_bit_multi_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(128);

  // Set bit in second word only
  ab.fetch_or_bit(100, std::memory_order_relaxed);

  size_t bit_out = 0;
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 100u);

  // Now set a bit in first word
  ab.fetch_or_bit(10, std::memory_order_relaxed);
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 10u);
}

TEST_F(CATEGORY, atomic_bitmap_find_first_set_bit_at_zero) {
  tmc::detail::atomic_bitmap ab;
  ab.init(64);

  ab.fetch_or_bit(0, std::memory_order_relaxed);

  size_t bit_out = 999;
  EXPECT_TRUE(ab.find_first_set_bit(bit_out, std::memory_order_relaxed));
  EXPECT_EQ(bit_out, 0u);
}

TEST_F(CATEGORY, popcount_and) {
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(8);
  mask.init(8);

  // Set bits 0, 2, 4, 6 in atomic_bitmap
  ab.fetch_or_bit(0, std::memory_order_relaxed);
  ab.fetch_or_bit(2, std::memory_order_relaxed);
  ab.fetch_or_bit(4, std::memory_order_relaxed);
  ab.fetch_or_bit(6, std::memory_order_relaxed);

  // Mask allows only bits 0, 1, 2, 3 (first half)
  mask.set_bit(0);
  mask.set_bit(1);
  mask.set_bit(2);
  mask.set_bit(3);

  // Without mask: 4 bits set (0, 2, 4, 6)
  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 4u);

  // With mask: only bits 0 and 2 are both set and allowed
  EXPECT_EQ(ab.popcount_and(mask, std::memory_order_relaxed), 2u);
}

TEST_F(CATEGORY, popcount_and_multi_word) {
  // Test popcount_and with more than 64 bits (multi-word bitmap)
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(128);
  mask.init(128);

  // Set bits in first word (0-63) and second word (64-127)
  ab.fetch_or_bit(0, std::memory_order_relaxed);
  ab.fetch_or_bit(63, std::memory_order_relaxed);
  ab.fetch_or_bit(64, std::memory_order_relaxed);
  ab.fetch_or_bit(127, std::memory_order_relaxed);

  // Mask allows only the first word (bits 0-63)
  for (size_t i = 0; i < 64; ++i) {
    mask.set_bit(i);
  }

  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 4u);
  EXPECT_EQ(ab.popcount_and(mask, std::memory_order_relaxed), 2u);
}

TEST_F(CATEGORY, popcount_and_empty_mask) {
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(64);
  mask.init(64);

  ab.fetch_or_bit(0, std::memory_order_relaxed);
  ab.fetch_or_bit(32, std::memory_order_relaxed);
  ab.fetch_or_bit(63, std::memory_order_relaxed);

  // Empty mask - no bits allowed
  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 3u);
  EXPECT_EQ(ab.popcount_and(mask, std::memory_order_relaxed), 0u);
}

TEST_F(CATEGORY, popcount_and_partial_word) {
  tmc::detail::atomic_bitmap ab;
  tmc::detail::bitmap mask;

  ab.init(70); // Partial second word
  mask.init(70);

  ab.fetch_or_bit(0, std::memory_order_relaxed);
  ab.fetch_or_bit(65, std::memory_order_relaxed);
  ab.fetch_or_bit(69, std::memory_order_relaxed);

  mask.set_bit(0);
  mask.set_bit(69);

  EXPECT_EQ(ab.popcount(std::memory_order_relaxed), 3u);
  EXPECT_EQ(ab.popcount_and(mask, std::memory_order_relaxed), 2u);
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

#undef CATEGORY

#endif // TMC_MORE_THREADS

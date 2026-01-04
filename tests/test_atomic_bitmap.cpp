#include "test_common.hpp"
#include "tmc/detail/atomic_bitmap.hpp"
#include "tmc/detail/compat.hpp"

#include <gtest/gtest.h>

#define CATEGORY test_atomic_bitmap

class CATEGORY : public testing::Test {
protected:
  static void SetUpTestSuite() {
    tmc::cpu_executor().set_thread_count(4).init();
  }

  static void TearDownTestSuite() { tmc::cpu_executor().teardown(); }

  static tmc::ex_cpu& ex() { return tmc::cpu_executor(); }
};

TEST_F(CATEGORY, atomic_bitmap_init_and_clear) {
  tmc::detail::atomic_bitmap ab;

  ab.init(TMC_PLATFORM_BITS);
  EXPECT_EQ(ab.get_word_count(), 1u);

  ab.clear();
  EXPECT_EQ(ab.get_word_count(), 1u);

// Reinit after clear
#ifdef TMC_MORE_THREADS
  ab.init(TMC_PLATFORM_BITS * 2);
  EXPECT_EQ(ab.get_word_count(), 2u);
#else
  ab.init(TMC_PLATFORM_BITS);
  EXPECT_EQ(ab.get_word_count(), 1u);
#endif
}

TEST_F(CATEGORY, atomic_bitmap_init_various_sizes) {
  tmc::detail::atomic_bitmap ab;

  // Single bit
  ab.init(1);
  EXPECT_EQ(ab.get_word_count(), 1u);
  ab.clear();

  // Exactly one word
  ab.init(TMC_PLATFORM_BITS);
  EXPECT_EQ(ab.get_word_count(), 1u);
  ab.clear();

#ifdef TMC_MORE_THREADS
  // Just over one word
  ab.init(TMC_PLATFORM_BITS + 1);
  EXPECT_EQ(ab.get_word_count(), 2u);
  ab.clear();

  // Multiple words
  ab.init(TMC_PLATFORM_BITS * 4);
  EXPECT_EQ(ab.get_word_count(), 4u);
#endif
}

TEST_F(CATEGORY, atomic_bitmap_set_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));

  ab.set_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(ab.load_word(0), 1u);
  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));

  ab.set_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(ab.load_word(0), 1u);
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

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

#ifdef TMC_MORE_THREADS
TEST_F(CATEGORY, atomic_bitmap_test_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS * 24);

  // Test bits across word boundaries
  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(TMC_PLATFORM_BITS, std::memory_order_relaxed));
  EXPECT_FALSE(
    ab.test_bit(TMC_PLATFORM_BITS * 2 - 1, std::memory_order_relaxed)
  );

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS * 2 - 1, std::memory_order_relaxed);

  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(TMC_PLATFORM_BITS, std::memory_order_relaxed));
  EXPECT_TRUE(
    ab.test_bit(TMC_PLATFORM_BITS * 2 - 1, std::memory_order_relaxed)
  );
}
TEST_F(CATEGORY, atomic_bitmap_load_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS * 2);

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(1, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS, std::memory_order_relaxed);

  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), 3u);
  EXPECT_EQ(ab.load_word(1, std::memory_order_relaxed), 1u);
}
#else
TEST_F(CATEGORY, atomic_bitmap_test_bit) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed);

  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));
}
TEST_F(CATEGORY, atomic_bitmap_load_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(1, std::memory_order_relaxed);

  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), 3u);
}
#endif

TEST_F(CATEGORY, atomic_bitmap_popcnt_single_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

  EXPECT_EQ(ab.popcnt(), 0u);

  ab.set_bit(0, std::memory_order_relaxed);
  EXPECT_EQ(ab.popcnt(), 1u);

  ab.set_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed);
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

#ifdef TMC_MORE_THREADS
TEST_F(CATEGORY, atomic_bitmap_popcnt_multi_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS * 2);

  ab.set_bit(0, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS, std::memory_order_relaxed);
  ab.set_bit(TMC_PLATFORM_BITS * 2 - 1, std::memory_order_relaxed);

  EXPECT_EQ(ab.popcnt(), 4u);
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
  ab1.init(TMC_PLATFORM_BITS);
  ab2.init(TMC_PLATFORM_BITS);

  ab1.set_bit(0, std::memory_order_relaxed);

  size_t result = ab1.load_inverted_or(ab2, 0, std::memory_order_relaxed);
  EXPECT_EQ(result, ~size_t(1));
}
#endif

TEST_F(CATEGORY, bitmap_init_and_clear) {
  tmc::detail::bitmap bm;

  bm.init(TMC_PLATFORM_BITS);
  bm.set_bit(0);
  EXPECT_TRUE(bm.test_bit(0));

  bm.clear();

// Reinit after clear
#ifdef TMC_MORE_THREADS
  bm.init(TMC_PLATFORM_BITS * 2);
#else
  bm.init(TMC_PLATFORM_BITS);
#endif
  EXPECT_FALSE(bm.test_bit(0));
}

TEST_F(CATEGORY, bitmap_move_constructor) {
  tmc::detail::bitmap bm1;
  bm1.init(TMC_PLATFORM_BITS);
  bm1.set_bit(5);
  bm1.set_bit(10);

  tmc::detail::bitmap bm2(std::move(bm1));

  EXPECT_TRUE(bm2.test_bit(5));
  EXPECT_TRUE(bm2.test_bit(10));
  EXPECT_FALSE(bm2.test_bit(0));
}

TEST_F(CATEGORY, bitmap_move_assignment) {
  tmc::detail::bitmap bm1;
  bm1.init(TMC_PLATFORM_BITS);
  bm1.set_bit(15);

  tmc::detail::bitmap bm2;
  bm2.init(TMC_PLATFORM_BITS / 2);
  bm2.set_bit(0);

  bm2 = std::move(bm1);

  EXPECT_TRUE(bm2.test_bit(15));
  EXPECT_FALSE(bm2.test_bit(0));
}

TEST_F(CATEGORY, bitmap_move_assignment_self) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS);
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

TEST_F(CATEGORY, bitmap_set_bit_idempotent) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS);

  bm.set_bit(10);
  EXPECT_TRUE(bm.test_bit(10));

  bm.set_bit(10); // Set again
  EXPECT_TRUE(bm.test_bit(10));
}

#ifdef TMC_MORE_THREADS
TEST_F(CATEGORY, bitmap_set_and_test_bit) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS * 2);

  EXPECT_FALSE(bm.test_bit(0));
  EXPECT_FALSE(bm.test_bit(TMC_PLATFORM_BITS - 1));
  EXPECT_FALSE(bm.test_bit(TMC_PLATFORM_BITS));
  EXPECT_FALSE(bm.test_bit(TMC_PLATFORM_BITS * 2 - 1));

  bm.set_bit(0);
  bm.set_bit(TMC_PLATFORM_BITS - 1);
  bm.set_bit(TMC_PLATFORM_BITS);
  bm.set_bit(TMC_PLATFORM_BITS * 2 - 1);

  EXPECT_TRUE(bm.test_bit(0));
  EXPECT_TRUE(bm.test_bit(TMC_PLATFORM_BITS - 1));
  EXPECT_TRUE(bm.test_bit(TMC_PLATFORM_BITS));
  EXPECT_TRUE(bm.test_bit(TMC_PLATFORM_BITS * 2 - 1));

  // Non-set bits remain false
  EXPECT_FALSE(bm.test_bit(1));
  EXPECT_FALSE(bm.test_bit(TMC_PLATFORM_BITS - 2));
  EXPECT_FALSE(bm.test_bit(TMC_PLATFORM_BITS + 1));
}

TEST_F(CATEGORY, bitmap_load_word) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS * 2);

  bm.set_bit(0);
  bm.set_bit(1);
  bm.set_bit(TMC_PLATFORM_BITS);

  EXPECT_EQ(bm.load_word(0), 3u);
  EXPECT_EQ(bm.load_word(1), 1u);
}
#else
TEST_F(CATEGORY, bitmap_set_and_test_bit) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS);

  EXPECT_FALSE(bm.test_bit(0));
  EXPECT_FALSE(bm.test_bit(TMC_PLATFORM_BITS - 1));

  bm.set_bit(0);
  bm.set_bit(TMC_PLATFORM_BITS - 1);

  EXPECT_TRUE(bm.test_bit(0));
  EXPECT_TRUE(bm.test_bit(TMC_PLATFORM_BITS - 1));

  // Non-set bits remain false
  EXPECT_FALSE(bm.test_bit(1));
  EXPECT_FALSE(bm.test_bit(TMC_PLATFORM_BITS - 2));
}

TEST_F(CATEGORY, bitmap_load_word) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS);

  bm.set_bit(0);
  bm.set_bit(1);

  EXPECT_EQ(bm.load_word(0), 3u);
}
#endif

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
  bm.init(TMC_PLATFORM_BITS);
  bm.set_bit(TMC_PLATFORM_BITS - 1);
  EXPECT_TRUE(bm.test_bit(TMC_PLATFORM_BITS - 1));
  bm.clear();

#ifdef TMC_MORE_THREADS
  // Just over one word
  bm.init(TMC_PLATFORM_BITS + 1);
  bm.set_bit(TMC_PLATFORM_BITS);
  EXPECT_TRUE(bm.test_bit(TMC_PLATFORM_BITS));
#endif
}

#ifdef TMC_MORE_THREADS
TEST_F(CATEGORY, atomic_bitmap_set_bit_popcnt) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS * 2);

  EXPECT_EQ(ab.set_bit_popcnt(0), 1u);
  EXPECT_EQ(ab.set_bit_popcnt(TMC_PLATFORM_BITS - 1), 2u);
  EXPECT_EQ(ab.set_bit_popcnt(TMC_PLATFORM_BITS), 3u);
  EXPECT_EQ(ab.set_bit_popcnt(TMC_PLATFORM_BITS * 2 - 1), 4u);

  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(TMC_PLATFORM_BITS, std::memory_order_relaxed));
  EXPECT_TRUE(
    ab.test_bit(TMC_PLATFORM_BITS * 2 - 1, std::memory_order_relaxed)
  );
}

TEST_F(CATEGORY, atomic_bitmap_set_bit_popcnt_partial_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS + 6);

  EXPECT_EQ(ab.set_bit_popcnt(0), 1u);
  EXPECT_EQ(ab.set_bit_popcnt(TMC_PLATFORM_BITS + 5), 2u);
  EXPECT_EQ(ab.popcnt(), 2u);
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit_popcnt) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS * 2);

  ab.set_bit(0);
  ab.set_bit(TMC_PLATFORM_BITS - 1);
  ab.set_bit(TMC_PLATFORM_BITS);
  ab.set_bit(TMC_PLATFORM_BITS * 2 - 1);

  EXPECT_EQ(ab.clr_bit_popcnt(TMC_PLATFORM_BITS * 2 - 1), 3u);
  EXPECT_EQ(ab.clr_bit_popcnt(TMC_PLATFORM_BITS), 2u);
  EXPECT_EQ(ab.clr_bit_popcnt(TMC_PLATFORM_BITS - 1), 1u);
  EXPECT_EQ(ab.clr_bit_popcnt(0), 0u);

  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(TMC_PLATFORM_BITS, std::memory_order_relaxed));
  EXPECT_FALSE(
    ab.test_bit(TMC_PLATFORM_BITS * 2 - 1, std::memory_order_relaxed)
  );
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit_popcnt_partial_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS + 6);

  ab.set_bit(0);
  ab.set_bit(TMC_PLATFORM_BITS + 5);

  EXPECT_EQ(ab.clr_bit_popcnt(TMC_PLATFORM_BITS + 5), 1u);
  EXPECT_EQ(ab.clr_bit_popcnt(0), 0u);
}

TEST_F(CATEGORY, bitmap_get_word_count) {
  tmc::detail::bitmap bm;

  bm.init(1);
  EXPECT_EQ(bm.get_word_count(), 1u);
  bm.clear();

  bm.init(TMC_PLATFORM_BITS);
  EXPECT_EQ(bm.get_word_count(), 1u);
  bm.clear();

  bm.init(TMC_PLATFORM_BITS + 1);
  EXPECT_EQ(bm.get_word_count(), 2u);
  bm.clear();

  bm.init(TMC_PLATFORM_BITS * 2);
  EXPECT_EQ(bm.get_word_count(), 2u);
  bm.clear();

  bm.init(TMC_PLATFORM_BITS * 4);
  EXPECT_EQ(bm.get_word_count(), 4u);
}

TEST_F(CATEGORY, bitmap_valid_mask_for_word) {
  tmc::detail::bitmap bm;

  bm.init(TMC_PLATFORM_BITS);
  EXPECT_EQ(bm.valid_mask_for_word(0), TMC_ALL_ONES);
  bm.clear();

  bm.init(TMC_PLATFORM_BITS * 2);
  EXPECT_EQ(bm.valid_mask_for_word(0), TMC_ALL_ONES);
  EXPECT_EQ(bm.valid_mask_for_word(1), TMC_ALL_ONES);
  bm.clear();

  bm.init(10);
  EXPECT_EQ(bm.valid_mask_for_word(0), (size_t(1) << 10) - 1);
  bm.clear();

  bm.init(TMC_PLATFORM_BITS + 6);
  EXPECT_EQ(bm.valid_mask_for_word(0), TMC_ALL_ONES);
  EXPECT_EQ(bm.valid_mask_for_word(1), (size_t(1) << 6) - 1);
}

TEST_F(CATEGORY, bitmap_popcnt) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS * 2);

  EXPECT_EQ(bm.popcnt(), 0u);

  bm.set_bit(0);
  EXPECT_EQ(bm.popcnt(), 1u);

  bm.set_bit(TMC_PLATFORM_BITS - 1);
  EXPECT_EQ(bm.popcnt(), 2u);

  bm.set_bit(TMC_PLATFORM_BITS);
  EXPECT_EQ(bm.popcnt(), 3u);

  bm.set_bit(TMC_PLATFORM_BITS * 2 - 1);
  EXPECT_EQ(bm.popcnt(), 4u);
}
#else
TEST_F(CATEGORY, atomic_bitmap_set_bit_popcnt) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

  EXPECT_EQ(ab.set_bit_popcnt(0), 1u);
  EXPECT_EQ(ab.set_bit_popcnt(TMC_PLATFORM_BITS - 1), 2u);

  EXPECT_TRUE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_TRUE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));
}

TEST_F(CATEGORY, atomic_bitmap_set_bit_popcnt_partial_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(10);

  EXPECT_EQ(ab.set_bit_popcnt(0), 1u);
  EXPECT_EQ(ab.set_bit_popcnt(7), 2u);
  EXPECT_EQ(ab.popcnt(), 2u);
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit_popcnt) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

  ab.set_bit(0);
  ab.set_bit(TMC_PLATFORM_BITS - 1);

  EXPECT_EQ(ab.clr_bit_popcnt(TMC_PLATFORM_BITS - 1), 1u);
  EXPECT_EQ(ab.clr_bit_popcnt(0), 0u);

  EXPECT_FALSE(ab.test_bit(0, std::memory_order_relaxed));
  EXPECT_FALSE(ab.test_bit(TMC_PLATFORM_BITS - 1, std::memory_order_relaxed));
}

TEST_F(CATEGORY, atomic_bitmap_clr_bit_popcnt_partial_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(10);

  ab.set_bit(0);
  ab.set_bit(5);

  EXPECT_EQ(ab.clr_bit_popcnt(5), 1u);
  EXPECT_EQ(ab.clr_bit_popcnt(0), 0u);
}

TEST_F(CATEGORY, bitmap_get_word_count) {
  tmc::detail::bitmap bm;

  bm.init(1);
  EXPECT_EQ(bm.get_word_count(), 1u);
  bm.clear();

  bm.init(TMC_PLATFORM_BITS);
  EXPECT_EQ(bm.get_word_count(), 1u);
  bm.clear();
}

TEST_F(CATEGORY, bitmap_valid_mask_for_word) {
  tmc::detail::bitmap bm;

  bm.init(TMC_PLATFORM_BITS);
  EXPECT_EQ(bm.valid_mask_for_word(0), TMC_ALL_ONES);
}

TEST_F(CATEGORY, bitmap_popcnt) {
  tmc::detail::bitmap bm;
  bm.init(TMC_PLATFORM_BITS);

  EXPECT_EQ(bm.popcnt(), 0u);

  bm.set_bit(0);
  EXPECT_EQ(bm.popcnt(), 1u);

  bm.set_bit(TMC_PLATFORM_BITS - 1);
  EXPECT_EQ(bm.popcnt(), 2u);

  bm.set_bit(7);
  EXPECT_EQ(bm.popcnt(), 3u);
}

#endif

TEST_F(CATEGORY, bitmap_popcnt_partial_word) {
  tmc::detail::bitmap bm;
  bm.init(10);

  for (size_t i = 0; i < 10; ++i) {
    bm.set_bit(i);
  }

  EXPECT_EQ(bm.popcnt(), 10u);
}

#ifdef TMC_MORE_THREADS
TEST_F(CATEGORY, atomic_bitmap_valid_mask_for_word) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS + 6);
  ab.set_first_n_bits(TMC_PLATFORM_BITS + 6);
  EXPECT_EQ(ab.valid_mask_for_word(0), TMC_ALL_ONES);
  EXPECT_EQ(ab.valid_mask_for_word(1), (TMC_ONE_BIT << 6) - 1);
}
TEST_F(CATEGORY, atomic_bitmap_set_first_n_bits) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS * 2);

  // Set first 10 bits
  ab.set_first_n_bits(10);
  EXPECT_EQ(ab.popcnt(), 10u);
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(ab.test_bit(i, std::memory_order_relaxed));
  }
  for (size_t i = 10; i < TMC_PLATFORM_BITS * 2; ++i) {
    EXPECT_FALSE(ab.test_bit(i, std::memory_order_relaxed));
  }

  ab.clear();
  ab.init(TMC_PLATFORM_BITS * 2);

  // Set exactly one full word
  ab.set_first_n_bits(TMC_PLATFORM_BITS);
  EXPECT_EQ(ab.popcnt(), TMC_PLATFORM_BITS);
  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), TMC_ALL_ONES);
  EXPECT_EQ(ab.load_word(1, std::memory_order_relaxed), 0u);

  ab.clear();
  ab.init(TMC_PLATFORM_BITS * 2);

  // Set more than one word
  ab.set_first_n_bits(TMC_PLATFORM_BITS + 6);
  EXPECT_EQ(ab.popcnt(), TMC_PLATFORM_BITS + 6);
  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), TMC_ALL_ONES);
  EXPECT_EQ(ab.load_word(1, std::memory_order_relaxed), (TMC_ONE_BIT << 6) - 1);

  ab.clear();
  ab.init(TMC_PLATFORM_BITS * 2);

  // Set all bits
  ab.set_first_n_bits(TMC_PLATFORM_BITS * 2);
  EXPECT_EQ(ab.popcnt(), TMC_PLATFORM_BITS * 2);
  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), TMC_ALL_ONES);
  EXPECT_EQ(ab.load_word(1, std::memory_order_relaxed), TMC_ALL_ONES);

  ab.clear();
  ab.init(TMC_PLATFORM_BITS * 2);

  // Set zero bits
  ab.set_first_n_bits(0);
  EXPECT_EQ(ab.popcnt(), 0u);
}
#else
TEST_F(CATEGORY, atomic_bitmap_set_first_n_bits) {
  tmc::detail::atomic_bitmap ab;
  ab.init(TMC_PLATFORM_BITS);

  // Set first 10 bits
  ab.set_first_n_bits(10);
  EXPECT_EQ(ab.popcnt(), 10u);
  for (size_t i = 0; i < 10; ++i) {
    EXPECT_TRUE(ab.test_bit(i, std::memory_order_relaxed));
  }
  for (size_t i = 10; i < TMC_PLATFORM_BITS; ++i) {
    EXPECT_FALSE(ab.test_bit(i, std::memory_order_relaxed));
  }

  ab.clear();
  ab.init(TMC_PLATFORM_BITS);

  // Set all bits in single word
  ab.set_first_n_bits(TMC_PLATFORM_BITS);
  EXPECT_EQ(ab.popcnt(), TMC_PLATFORM_BITS);
  EXPECT_EQ(ab.load_word(0, std::memory_order_relaxed), TMC_ALL_ONES);

  ab.clear();
  ab.init(TMC_PLATFORM_BITS);

  // Set zero bits
  ab.set_first_n_bits(0);
  EXPECT_EQ(ab.popcnt(), 0u);
}
#endif

#undef CATEGORY

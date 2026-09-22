#include "tmc/channel.hpp"

#include <gtest/gtest.h>

#ifndef NDEBUG
#define CATEGORY assert_channel_DeathTest

// Data operations on an empty (default-constructed or moved-from) token
// should trigger the debug assert rather than dereference null.

TEST(CATEGORY, empty_token_post) {
  EXPECT_DEATH(
    {
      tmc::chan_tok<int> tok;
      tok.post(1);
    },
    "empty"
  );
}

TEST(CATEGORY, empty_token_try_pull) {
  EXPECT_DEATH(
    {
      tmc::chan_tok<int> tok;
      tok.try_pull();
    },
    "empty"
  );
}

TEST(CATEGORY, empty_token_close) {
  EXPECT_DEATH(
    {
      tmc::chan_tok<int> tok;
      tok.close();
    },
    "empty"
  );
}

TEST(CATEGORY, moved_from_token_post) {
  EXPECT_DEATH(
    {
      auto tok = tmc::make_channel<int>();
      auto tok2 = std::move(tok);
      tok.post(1);
    },
    "empty"
  );
}

TEST(CATEGORY, empty_token_set_config) {
  EXPECT_DEATH(
    {
      tmc::chan_tok<int> tok;
      tok.set_reuse_blocks(true);
    },
    "empty"
  );
}

#undef CATEGORY
#endif

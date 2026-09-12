#include "test_framework.h"
#include "../../src/util/byte_cache.h"

TEST(byte_cache_evicts_the_least_recently_used_value) {
    ByteCache<int> cache(800);
    ASSERT_TRUE(cache.put("first", 1, 100));
    ASSERT_TRUE(cache.put("second", 2, 100));
    ASSERT_EQ(*cache.get("first"), 1);
    ASSERT_TRUE(cache.put("third", 3, 100));
    ASSERT_TRUE(cache.get("first") != nullptr);
    ASSERT_TRUE(cache.get("second") == nullptr);
    ASSERT_EQ(*cache.get("third"), 3);
    ASSERT_TRUE(cache.bytes() <= 800u);
}

TEST(byte_cache_replacements_and_oversize_entries_preserve_the_budget) {
    ByteCache<int> cache(800);
    ASSERT_TRUE(cache.put("first", 1, 100));
    const auto bytes = cache.bytes();
    ASSERT_TRUE(cache.put("first", 2, 100));
    ASSERT_EQ(cache.bytes(), bytes);
    ASSERT_EQ(cache.size(), 1u);
    ASSERT_FALSE(cache.put("oversized", 3, 10000));
    ASSERT_EQ(cache.bytes(), bytes);
    ASSERT_FALSE(cache.put(std::string(1000, 'k'), 3, 0));
    ASSERT_EQ(*cache.get("first"), 2);
}

int main() { RUN_ALL_TESTS(); }

#include "test_framework.h"
#include "../../src/ui/token_cache.h"

TEST(syntax_cache_reuses_tokens_without_caching_theme_colors) {
    code_highlight::TokenCache cache;
    auto first = cache.get("int answer = 42;", "first.cpp");
    auto same = cache.get("int answer = 42;", "different.cpp");
    ASSERT_EQ(first, same);
    ASSERT_EQ(cache.hits(), 1u);
    ASSERT_EQ(cache.misses(), 1u);
    ASSERT_EQ(first->front().kind, code_highlight::Kind::Keyword);
    auto plain = cache.get("int answer = 42;", "file.txt");
    ASSERT_NE(first, plain);
    ASSERT_EQ(plain->front().kind, code_highlight::Kind::Plain);
    ASSERT_NE(first, cache.get("int answer = 43;", "first.cpp"));
}

TEST(syntax_cache_budget_does_not_prevent_oversized_lines_from_rendering) {
    code_highlight::TokenCache cache(1024);
    auto tokens = cache.get(std::string(5000, 'x'), "file.cpp");
    ASSERT_EQ(tokens->front().text.size(), 5000u);
    ASSERT_TRUE(cache.bytes() <= 1024u);
    for (int i = 0; i < 100; ++i) cache.get("int value = " + std::to_string(i), "file.cpp");
    ASSERT_TRUE(cache.bytes() <= 1024u);
}

int main() { RUN_ALL_TESTS(); }

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

TEST(published_lines_reuse_tokens_with_separate_language_whitespace_and_revision_keys) {
    code_highlight::TokenCache cache;
    const std::string text = "int\tvalue = 42;\r";
    auto first = cache.get_source(text, "file.cpp", false, "99:a:1");
    ASSERT_EQ(first, cache.get_source(text, "another.cpp", false, "99:a:1"));
    ASSERT_EQ(cache.hits(), 1u);
    ASSERT_NE(first, cache.get_source(text, "file.cpp", true, "99:a:1"));
    ASSERT_NE(first, cache.get_source(text, "file.cpp", false, "99:b:1"));
    ASSERT_NE(first, cache.get_source("changed", "file.cpp", false, "100:a:1"));
    ASSERT_NE(first, cache.get_source(text, "file.txt", false, "99:a:1"));
    std::string joined;
    for (const auto& token : *first) joined += token.text;
    ASSERT_EQ(joined, code_highlight::display_text(text, false));
    const std::string longLine(768 * 1024, 'x');
    const auto longTokens = cache.get_source(longLine, "file.txt", false, "101:a:1");
    ASSERT_EQ(longTokens->front().text, longLine);
    ASSERT_EQ(longTokens, cache.get_source(longLine, "file.txt", false, "101:a:1"));
    ASSERT_TRUE(cache.bytes() <= 4 * 1024 * 1024u);
}

TEST(incoming_lexical_state_is_part_of_both_token_cache_keys) {
    code_highlight::TokenCache cache;
    const auto comment = code_lexer::scan("/*", code_lexer::Language::Cpp);
    const auto before = cache.get_source("int answer;", "file.cpp", false, "one", comment);
    const auto after = cache.get_source("int answer;", "file.cpp", false, "one");
    ASSERT_NE(before, after);
    ASSERT_EQ(before->front().kind, code_highlight::Kind::Comment);
    ASSERT_EQ(after->front().kind, code_highlight::Kind::Keyword);
    ASSERT_EQ(before, cache.get_source("int answer;", "file.cpp", false, "one", comment));
    ASSERT_NE(cache.get("int answer;", "file.cpp", comment), cache.get("int answer;", "file.cpp"));
}

int main() { RUN_ALL_TESTS(); }

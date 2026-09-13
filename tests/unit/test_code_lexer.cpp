#include "test_framework.h"
#include "../../src/util/code_lexer.h"

using namespace code_lexer;

TEST(comments_and_strings_keep_state_across_physical_lines) {
    for (auto lang : {Language::C, Language::Cpp, Language::JavaScript}) {
        auto state = scan("int value; /* begin\r\n", lang);
        ASSERT_EQ(state.mode, Mode::BlockComment);
        ASSERT_EQ(scan("still inside\n", lang, state).mode, Mode::BlockComment);
        ASSERT_EQ(scan("end */ int other;\n", lang, state), State{});
    }
    auto python = scan("message = \"\"\"first\n", Language::Python);
    ASSERT_EQ(python.mode, Mode::TripleDouble);
    ASSERT_EQ(scan("second\n", Language::Python, python).mode, Mode::TripleDouble);
    ASSERT_EQ(scan("last\"\"\"\n", Language::Python, python), State{});
    auto javascript = scan("const message = `first\n", Language::JavaScript);
    ASSERT_EQ(javascript.mode, Mode::Template);
    ASSERT_EQ(scan("last`;\n", Language::JavaScript, javascript), State{});
}

TEST(raw_delimiters_and_escaped_newlines_are_preserved) {
    const auto raw = scan("R\"delimiter(first\n", Language::Cpp);
    ASSERT_EQ(raw.mode, Mode::Raw);
    ASSERT_EQ(scan("wrong)other\"\n", Language::Cpp, raw).mode, Mode::Raw);
    ASSERT_EQ(scan("last)delimiter\";\n", Language::Cpp, raw), State{});
    auto quoted = scan("\"first\\\r\n", Language::C);
    ASSERT_EQ(quoted.mode, Mode::Double);
    ASSERT_EQ(scan("last\"\n", Language::C, quoted), State{});
    ASSERT_EQ(scan("\"broken\n", Language::C), State{});
    auto comment = scan("// continued\\\r\n", Language::Cpp);
    ASSERT_EQ(comment.mode, Mode::LineComment);
    ASSERT_EQ(scan("last\n", Language::Cpp, comment), State{});
    ASSERT_EQ(scan("# ended\\\n", Language::Python), State{});
    ASSERT_EQ(scan_line("\"first\\\r", true, Language::C).mode, Mode::Double);
    ASSERT_EQ(scan("total // 2\n", Language::Python), State{});
}

TEST(page_cursors_can_split_every_delimiter_with_bounded_lookahead) {
    for (const auto& text : {std::string("/* body */ int value;"), std::string("R\"abcdefghijklmnop(body)abcdefghijklmnop\";"),
                            std::string("\"a\\\"b\";"), std::string("// comment\nnext")}) {
        const auto expected = scan(text, Language::Cpp);
        for (size_t split = 0; split <= text.size(); ++split) {
            State state;
            for (size_t i = 0; i < split; ++i) advance_with_lookahead(state, Language::Cpp, std::string_view(text).substr(i, lookaheadSize));
            auto resumed = state;
            for (size_t i = split; i < text.size(); ++i) advance_with_lookahead(resumed, Language::Cpp, std::string_view(text).substr(i, lookaheadSize));
            ASSERT_EQ(resumed, expected);
        }
    }
}

TEST(before_and_after_states_are_independent_values) {
    auto before = scan("/* before\n", Language::Cpp);
    State after;
    ASSERT_EQ(advance_with_lookahead(before, Language::Cpp, "int value;"), Region::Comment);
    ASSERT_EQ(advance_with_lookahead(after, Language::Cpp, "int value;"), Region::Code);
    ASSERT_NE(before.key(), after.key());
    auto otherDelimiter = scan("R\"other(body", Language::Cpp);
    auto delimiter = scan("R\"one(body", Language::Cpp);
    ASSERT_NE(otherDelimiter.key(), delimiter.key());
    ASSERT_EQ(language("file.mm"), Language::Cpp);
    ASSERT_EQ(language("file.m"), Language::C);
    ASSERT_EQ(language("file.ts"), Language::JavaScript);
    ASSERT_EQ(language("file.txt"), Language::Plain);
    ASSERT_EQ(scan("/* plain", Language::Plain), State{});
}

int main() { RUN_ALL_TESTS(); }

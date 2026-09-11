#include "test_framework.h"
#include "../../src/ecs/components.h"
#include "../../src/ui/code_highlight.h"
#include "../../src/util/fuzzy_match.h"

TEST(fuzzy_paths_match_subsequences_and_rank_boundaries) {
    auto paths = fuzzy::rank({"src/application.cpp", "src/app.cpp", "README.md"}, "sacp");
    ASSERT_EQ(paths.size(), 2u);
    ASSERT_EQ(paths.front(), "src/app.cpp");
    ASSERT_TRUE(fuzzy::score("README", "readme.md").has_value());
    ASSERT_FALSE(fuzzy::score("xyz", "src/app.cpp").has_value());
}

TEST(syntax_tokens_preserve_source_and_strings) {
    std::string source = "const int n = 42; // sample";
    auto tokens = code_highlight::tokenize(source, "main.cpp");
    std::string joined;
    bool keyword = false, number = false, comment = false;
    for (const auto& token : tokens) {
        joined += token.text;
        keyword |= token.kind == code_highlight::Kind::Keyword;
        number |= token.kind == code_highlight::Kind::Number;
        comment |= token.kind == code_highlight::Kind::Comment;
    }
    ASSERT_EQ(joined, source);
    ASSERT_TRUE(keyword && number && comment);
    auto literal = code_highlight::tokenize("\"// literal\"", "main.cpp");
    ASSERT_EQ(literal.size(), 1u);
    ASSERT_EQ(literal.front().kind, code_highlight::Kind::String);
    ASSERT_EQ(code_highlight::tokenize(source, "notes.txt").size(), 1u);
}

TEST(diff_find_tracks_side_line_and_occurrence) {
    ecs::FileDiff file;
    file.filePath = "code.cpp";
    file.hunks.push_back({10, 2, 20, 2, "@@ -10,2 +20,2 @@",
                          {" needle needle", "-old needle", "+new needle"}});
    auto matches = ecs::find_diff_matches({file}, "needle");
    ASSERT_EQ(matches.size(), 4u);
    ASSERT_EQ(matches[0].line, 20);
    ASSERT_EQ(matches[0].column, 0u);
    ASSERT_EQ(matches[1].column, 7u);
    ASSERT_EQ(matches[2].line, 11);
    ASSERT_EQ(matches[2].sign, '-');
    ASSERT_EQ(matches[3].line, 21);
    ASSERT_EQ(matches[3].sign, '+');
    ASSERT_TRUE(ecs::find_diff_matches({file}, "").empty());
    ASSERT_TRUE(ecs::find_diff_matches({file}, "absent").empty());
}

TEST(intraline_ranges_keep_shared_prefix_and_suffix_unmarked) {
    auto [before, after] = code_highlight::changed_ranges("return one;", "return two;");
    ASSERT_EQ(before.first, 7u);
    ASSERT_EQ(before.second, 10u);
    ASSERT_EQ(after, before);
    auto same = code_highlight::changed_ranges("same", "same");
    ASSERT_EQ(same.first.first, same.first.second);
    auto ranges = code_highlight::hunk_ranges({" context", "-return one;", "+return two;", "+unpaired"});
    ASSERT_EQ(ranges[1], before);
    ASSERT_EQ(ranges[2], after);
    ASSERT_EQ(ranges[3].second, 0u);
}

TEST(whitespace_display_distinguishes_line_endings_without_mutating_source) {
    std::string raw = "\tvalue  \r";
    ASSERT_EQ(code_highlight::display_text(raw, true, true), "→   value·· [CRLF]");
    ASSERT_EQ(code_highlight::display_text(raw, false), "    value  ");
    ASSERT_EQ(code_highlight::display_text("value", true, true, false), "value [no newline]");
    ASSERT_EQ(raw, "\tvalue  \r");
}

int main() { RUN_ALL_TESTS(); }

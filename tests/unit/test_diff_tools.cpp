#include "test_framework.h"
#include "../../src/ecs/components.h"
#include "../../src/ui/code_highlight.h"
#include "../../src/util/fuzzy_match.h"
#include "../../src/util/file_tree.h"
#include "../../src/util/diff_revisions.h"
#include "../../src/util/commit_graph.h"
#include "../../src/util/review_selection.h"
#include "../../src/util/navigation.h"

TEST(navigation_history_truncates_forward_after_a_new_destination) {
    ecs::NavigationHistory history;
    using Kind = ecs::NavigationLocation::Kind;
    ecs::NavigationLocation first{Kind::File, "first.txt"};
    ecs::NavigationLocation second{Kind::File, "second.txt"};
    ecs::NavigationLocation commit{Kind::Commit, "", "sha"};
    navigation::record(history, first);
    navigation::record(history, first);
    ASSERT_EQ(history.entries.size(), 1u);
    ASSERT_FALSE(navigation::step(history, -1).has_value());
    navigation::record(history, second);
    ASSERT_EQ(*navigation::step(history, -1), first);
    ASSERT_EQ(*navigation::step(history, 1), second);
    ASSERT_EQ(*navigation::step(history, -1), first);
    navigation::record(history, commit);
    ASSERT_FALSE(navigation::step(history, 1).has_value());
    ASSERT_EQ(history.entries.size(), 2u);
    ASSERT_EQ(*navigation::step(history, -1), first);
}

TEST(navigation_records_the_visible_review_destination) {
    ecs::RepoComponent repo;
    repo.selectedFilePath = "file.txt";
    auto normal = navigation::location(repo);
    auto reviewing = navigation::location(repo, true);
    ASSERT_EQ(normal.kind, ecs::NavigationLocation::Kind::File);
    ASSERT_EQ(reviewing.kind, ecs::NavigationLocation::Kind::WorkingTree);
    ASSERT_TRUE(reviewing.reviewing);
    repo.fullFilePath = "file.txt";
    ASSERT_EQ(navigation::location(repo, true).kind, ecs::NavigationLocation::Kind::FullFile);
}

TEST(review_ranges_keep_old_and_new_line_numbers_distinct) {
    auto added = review_selection::range({{0, 20}, {0, 21}});
    ASSERT_EQ(added->first, 20);
    ASSERT_EQ(added->last, 21);
    ASSERT_FALSE(added->oldSide);
    auto removed = review_selection::range({{8, 0}, {7, 0}, {6, 15}});
    ASSERT_EQ(removed->first, 6);
    ASSERT_EQ(removed->last, 8);
    ASSERT_TRUE(removed->oldSide);
    ASSERT_FALSE(review_selection::range({{1, 0}, {0, 2}}).has_value());
    ASSERT_FALSE(review_selection::range({}).has_value());
}

TEST(commit_graph_tracks_forks_merges_and_roots) {
    auto entry = [](const std::string& hash, const std::string& parents) {
        ecs::CommitEntry commit;
        commit.hash = hash;
        commit.parentHashes = parents;
        return commit;
    };
    auto graph = commit_graph::build({entry("merge", "left right"), entry("right", "root"), entry("left", "root"), entry("root", "")});
    ASSERT_EQ(graph.columns, 2u);
    ASSERT_EQ(graph.rows.at("merge").parents.size(), 2u);
    ASSERT_FALSE(graph.rows.at("merge").incoming);
    ASSERT_EQ(graph.rows.at("right").lane, 1u);
    ASSERT_EQ(graph.rows.at("left").parents.front(), graph.rows.at("root").lane);
    ASSERT_TRUE(graph.rows.at("root").parents.empty());
    ASSERT_TRUE(graph.rows.at("root").continuing.empty());
    auto linear = commit_graph::build({entry("tip", "root"), entry("root", "")});
    ASSERT_EQ(linear.columns, 1u);
}

TEST(diff_revision_pairs_keep_comparisons_and_index_distinct) {
    ASSERT_EQ(diff_revisions("compare:abc:def"), (std::pair<std::string, std::string>{"abc", "def"}));
    ASSERT_EQ(diff_revisions("wt"), (std::pair<std::string, std::string>{"INDEX", ""}));
    ASSERT_EQ(diff_revisions("index"), (std::pair<std::string, std::string>{"HEAD", "INDEX"}));
    ASSERT_EQ(diff_revisions("file:abc").second, "abc");
}

TEST(history_search_validates_dates_and_keeps_filters_literal) {
    git::HistoryQuery query{"a.*b", "Ada", "2024-02-29", "2024-03-01", "src/[file].cpp"};
    auto args = git::history_search_args(query, 200);
    ASSERT_TRUE(args.has_value());
    ASSERT_EQ(args->back(), ":(literal)src/[file].cpp");
    ASSERT_TRUE(std::find(args->begin(), args->end(), "--grep=a.*b") != args->end());
    ASSERT_TRUE(std::find(args->begin(), args->end(), "--fixed-strings") != args->end());
    query.since = "2023-02-29";
    ASSERT_FALSE(git::history_search_args(query, 200).has_value());
    query.since = "2025-01-01";
    ASSERT_FALSE(git::history_search_args(query, 200).has_value());
}

TEST(file_tree_collapses_descendants_without_hiding_siblings) {
    std::vector<std::string> paths{"src/deep/a.cpp", "src/b.cpp", "src2/c.cpp", "README.md"};
    auto rows = file_tree::flatten(paths, {});
    ASSERT_EQ(rows.size(), 7u);
    auto collapsed = file_tree::flatten(paths, {"src/"});
    ASSERT_EQ(collapsed.size(), 4u);
    ASSERT_EQ(collapsed[1].path, "src/");
    ASSERT_TRUE(collapsed[1].directory);
    ASSERT_EQ(collapsed.back().path, "src2/c.cpp");
    ASSERT_EQ(collapsed.back().sourceIndex, 2u);
    ASSERT_EQ(file_tree::flatten(paths, {"src/deep/"}).size(), 6u);
}

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

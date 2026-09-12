#include "test_framework.h"
#include "../../src/ecs/components.h"
#include "../../src/ui/code_highlight.h"
#include "../../src/util/fuzzy_match.h"
#include "../../src/util/file_tree.h"
#include "../../src/util/diff_revisions.h"
#include "../../src/util/commit_graph.h"
#include "../../src/util/review_selection.h"
#include "../../src/util/navigation.h"
#include "../../src/util/wrap_text.h"
#include "../../src/util/code_wrap.h"
#include "../../src/util/visible_rows.h"
#include "../../src/util/file_content.h"
#include "../../src/util/revision_range.h"
#include <unistd.h>
#include "../../src/util/lfs_pointer.h"
#include <fstream>

#ifdef __APPLE__
TEST(code_wrap_keeps_composed_characters_together) {
    std::string family = "👨‍👩‍👧‍👦", flag = "🇺🇸", accent = "é";
    auto text = family + flag + accent;
    auto ends = code_wrap::character_ends(text);
    ASSERT_EQ(ends, (std::vector<size_t>{family.size(), family.size() + flag.size(), text.size()}));
    auto breaks = code_wrap::breaks(text, 1.f, [](std::string_view) { return 1.f; });
    ASSERT_EQ(breaks, (std::vector<size_t>{0, family.size(), family.size() + flag.size(), text.size()}));
}
#endif

TEST(code_wrap_preserves_bytes_and_decoded_boundaries) {
    std::string text = "  abc\t新しい🙂code  \r";
    auto measure = [](std::string_view glyph) { return glyph == "\t" ? 4.f : glyph == "\r" ? 0.f : 1.f; };
    auto breaks = code_wrap::breaks(text, 5.f, measure);
    std::string restored;
    for (size_t i = 0; i + 1 < breaks.size(); ++i) {
        auto part = text.substr(breaks[i], breaks[i + 1] - breaks[i]);
        ASSERT_FALSE(part.empty());
        ASSERT_TRUE((static_cast<unsigned char>(part.front()) & 0xc0) != 0x80);
        restored += part;
    }
    ASSERT_EQ(restored, text);
    ASSERT_EQ(code_wrap::breaks("", 5.f, measure).size(), 2u);
    ASSERT_EQ(code_wrap::breaks("abcdefgh", 3.f, measure), (std::vector<size_t>{0, 3, 6, 8}));
    ASSERT_EQ(code_wrap::intersect({2, 7}, 3, 6), (std::pair<size_t, size_t>{0, 3}));
}

TEST(lfs_availability_is_refreshed_with_repository_generation) {
    char pattern[] = "/tmp/fh-lfs-cache.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    lfs_pointer::Pointer pointer{std::string(64, 'a'), 3};
    auto object = std::filesystem::path(directory) / ".git/lfs/objects/aa/aa" / pointer.oid;
    ASSERT_FALSE(lfs_pointer::cached_availability(directory, pointer, 1));
    std::filesystem::create_directories(object.parent_path());
    { std::ofstream output(object); output << "abc"; }
    ASSERT_FALSE(lfs_pointer::cached_availability(directory, pointer, 1));
    ASSERT_TRUE(lfs_pointer::cached_availability(directory, pointer, 2));
}

TEST(lfs_pointers_require_the_version_digest_and_unsigned_size) {
    auto pointer = "version https://git-lfs.github.com/spec/v1\noid sha256:" + std::string(64, 'a') + "\nsize 1024\n";
    ASSERT_EQ(lfs_pointer::parse(pointer)->size, 1024u);
    ASSERT_EQ(lfs_pointer::parse(pointer)->oid, std::string(64, 'a'));
    ASSERT_FALSE(lfs_pointer::parse("ordinary source\n").has_value());
    ASSERT_FALSE(lfs_pointer::parse(pointer + "size 4\n").has_value());
    ASSERT_FALSE(lfs_pointer::parse("version https://git-lfs.github.com/spec/v1\noid sha256:../bad\nsize 5\n").has_value());
    ASSERT_FALSE(lfs_pointer::parse("version https://git-lfs.github.com/spec/v1\noid sha256:" + std::string(64, 'a') + "\nsize -1\n").has_value());
}

TEST(message_row_window_is_bounded_at_top_middle_and_end) {
    for (float offset : {0.f, 20000.f, 179500.f}) {
        auto [first, last] = visible_rows(10000, 18.f, 100.f, offset, 800.f);
        ASSERT_TRUE(first <= last);
        ASSERT_TRUE(last <= 10000u);
        ASSERT_TRUE(last - first < 140u);
    }
    ASSERT_EQ(visible_rows(10000, 18.f, 100.f, 180000.f, 800.f).second, 10000u);
    ASSERT_EQ(visible_rows(0, 18.f, 100.f, 0.f, 800.f).second, 0u);
    ASSERT_EQ(visible_rows(10000, 0.f, 0.f, 0.f, 0.f).second, 100u);
}
TEST(codeowners_uses_last_match_and_repository_relative_patterns) {
    auto owners = codeowners::parse("* @default\n*.cpp @cpp\n/src/** @source\n/src/private/\n");
    ASSERT_EQ(codeowners::owners_for(owners, "README.md"), "@default");
    ASSERT_EQ(codeowners::owners_for(owners, "tests/test.cpp"), "@cpp");
    ASSERT_EQ(codeowners::owners_for(owners, "src/deep/file.cpp"), "@source");
    ASSERT_TRUE(codeowners::owners_for(owners, "src/private/key.txt").empty());
    auto nested = codeowners::parse("/docs/**/guide.md @docs\n");
    ASSERT_EQ(codeowners::owners_for(nested, "docs/guide.md"), "@docs");
    ASSERT_EQ(codeowners::owners_for(nested, "docs/deep/guide.md"), "@docs");
    ASSERT_TRUE(codeowners::owners_for(nested, "other/docs/guide.md").empty());
}

TEST(codeowners_wildcards_are_bounded_and_lines_have_a_limit) {
    auto owners = codeowners::parse("a*a*a*a*a*a*a*a*b @team\ncache/ @cache\n");
    ASSERT_TRUE(codeowners::owners_for(owners, std::string(4000, 'a')).empty());
    ASSERT_EQ(codeowners::owners_for(owners, "deep/cache/file.cpp"), "@cache");
    ASSERT_FALSE(codeowners::parse("* " + std::string(4096, 'x')).error.empty());
}

TEST(symlink_reader_returns_target_text_without_following_it) {
    char pattern[] = "/tmp/fh-link-unit.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    auto path = std::filesystem::path(directory) / "link";
    std::filesystem::create_symlink("missing-target", path);
    auto result = file_content::read_working_file(path);
    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.mode, "120000");
    ASSERT_EQ(result.bytes, "missing-target");
    std::filesystem::remove(path);
    std::filesystem::remove(directory);
}

TEST(file_mode_lookup_uses_exact_nul_terminated_paths) {
    constexpr char bytes[] = "100644 blob abc\tother\0" "120000 blob def\todd:name\0";
    auto records = std::string(bytes, sizeof(bytes) - 1);
    ASSERT_EQ(file_content::mode_for_path(records, "odd:name"), "120000");
    ASSERT_TRUE(file_content::mode_for_path(records, "odd").empty());
}

TEST(review_files_sort_by_churn_with_path_ties_and_preserve_filters) {
    std::vector<ecs::FileDiff> files(3);
    files[0].filePath = "a.cpp";
    files[0].additions = 1;
    files[1].filePath = "z.cpp";
    files[1].additions = 8;
    files[2].filePath = "m.cpp";
    files[2].deletions = 8;
    review_files::Filter filter;
    ASSERT_EQ(ecs::visible_file_indices(files, filter), (std::vector<size_t>{0, 2, 1}));
    filter.sort = review_files::Sort::MostChanges;
    ASSERT_EQ(ecs::visible_file_indices(files, filter), (std::vector<size_t>{2, 1, 0}));
    filter.sort = review_files::Sort::FewestChanges;
    ASSERT_EQ(ecs::visible_file_indices(files, filter), (std::vector<size_t>{0, 2, 1}));
    filter.language = "Python";
    ASSERT_TRUE(ecs::visible_file_indices(files, filter).empty());
}

TEST(tooltip_wrapping_preserves_all_text_and_utf8_glyphs) {
    std::string source = "Long subject with spaces and 世界";
    auto measure = [](const std::string& text) {
        return static_cast<float>(std::count_if(text.begin(), text.end(), [](unsigned char c) { return (c & 0xc0) != 0x80; }));
    };
    auto lines = wrap_measured_text(source, 8.f, measure);
    std::string joined;
    for (const auto& line : lines) { ASSERT_TRUE(measure(line) <= 8.f); joined += line; }
    ASSERT_EQ(joined, source);
    ASSERT_EQ(wrap_measured_text("one\n\ntwo", 20.f, measure), (std::vector<std::string>{"one", "", "two"}));
}

TEST(navigation_history_truncates_forward_after_a_new_destination) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("wt", "first.txt"));
    navigation::open(repo, reading::review("wt", "first.txt"));
    ASSERT_EQ(repo.workspace().history().size(), 2u);
    navigation::open(repo, reading::review("wt", "second.txt"));
    navigation::step(repo, -1);
    ASSERT_EQ(repo.selectedFilePath(), "first.txt");
    navigation::step(repo, 1);
    ASSERT_EQ(repo.selectedFilePath(), "second.txt");
    navigation::step(repo, -1);
    navigation::open(repo, reading::review("sha"));
    navigation::step(repo, 1);
    ASSERT_EQ(repo.selectedCommitHash(), "sha");
    ASSERT_EQ(repo.workspace().history().size(), 3u);
    navigation::step(repo, -1);
    ASSERT_EQ(repo.selectedFilePath(), "first.txt");
}

TEST(source_navigation_preserves_the_review_and_its_origin) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("commit", "changed.cpp"), true);
    navigation::open(repo, reading::source("first.cpp", "old-commit", 17));
    navigation::open(repo, reading::source("second.cpp"));
    ASSERT_EQ(repo.selectedCommitHash(), "commit");
    navigation::step(repo, -1);
    ASSERT_TRUE(ecs::source_tab_active(repo));
    ASSERT_EQ(repo.fullFilePath(), "first.cpp");
    ASSERT_EQ(repo.fullFileRevision(), "old-commit");
    ASSERT_EQ(repo.fullFileTargetLine(), 17);
    navigation::return_to_review(repo);
    ASSERT_FALSE(ecs::source_tab_active(repo));
    ASSERT_EQ(repo.selectedCommitHash(), "commit");
    ASSERT_EQ(repo.selectedFilePath(), "changed.cpp");
}

TEST(source_tab_keeps_revision_when_review_is_active) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("new-commit"));
    navigation::open(repo, reading::source("old.cpp", "old-commit"));
    ASSERT_TRUE(ecs::source_tab_active(repo));
    navigation::activate(repo, reading::Slot::Review);
    ASSERT_FALSE(ecs::source_tab_active(repo));
    ASSERT_EQ(repo.selectedCommitHash(), "new-commit");
    ASSERT_EQ(repo.fullFilePath(), "old.cpp");
    ASSERT_EQ(repo.fullFileRevision(), "old-commit");
    navigation::activate(repo, reading::Slot::Source);
    navigation::close_source(repo);
    ASSERT_FALSE(ecs::source_tab_active(repo));
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
    ASSERT_EQ(collapsed[0].path, "src/");
    ASSERT_TRUE(collapsed[0].directory);
    ASSERT_EQ(collapsed[2].path, "src2/c.cpp");
    ASSERT_EQ(collapsed[2].sourceIndex, 2u);
    ASSERT_EQ(file_tree::flatten(paths, {"src/deep/"}).size(), 6u);
}

TEST(file_tree_preserves_input_rank_inside_directory_groups) {
    std::vector<std::string> paths{"src/z-large.cpp", "src/a-small.cpp", "docs/readme.md"};
    auto rows = file_tree::flatten(paths, {});
    ASSERT_EQ(rows.size(), 5u);
    ASSERT_EQ(rows[0].path, "src/");
    ASSERT_TRUE(rows[0].directory);
    ASSERT_EQ(rows[1].path, "src/z-large.cpp");
    ASSERT_EQ(rows[1].sourceIndex, 0u);
    ASSERT_EQ(rows[2].path, "src/a-small.cpp");
    ASSERT_EQ(rows[2].sourceIndex, 1u);
    ASSERT_EQ(rows[3].path, "docs/");
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

TEST(merge_parent_selection_preserves_review_and_source_provenance) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("parent:second:merge", "deleted.cpp"));
    ASSERT_EQ(repo.selectedCommitHash(), "merge");
    ASSERT_FALSE(repo.comparisonOpen());
    ASSERT_EQ(ecs::selected_commit_parent(repo), "second");
    ASSERT_EQ(ecs::commit_review_scope(repo), "parent:second:merge");
    ASSERT_EQ(diff_revisions(ecs::commit_review_scope(repo)), (std::pair<std::string, std::string>{"second", "merge"}));
    ecs::ReviewComponent review;
    review.verdicts["merge"] = {ReviewVerdict::Commented, ecs::review_target_signature({})};
    ASSERT_EQ(ecs::current_review_verdict(review, ecs::commit_review_scope(repo), {}), ReviewVerdict::InProgress);
    ASSERT_EQ(ecs::current_review_verdict(review, "merge", {}), ReviewVerdict::Commented);
    ecs::ReviewComponent::Comment comment;
    comment.scope = ecs::commit_review_scope(repo);
    comment.oldSide = true;
    comment.line = 1;
    ecs::DiffHunk hunk{1, 1, 0, 0, "@@ -1 +0,0 @@", {"-old content"}};
    ASSERT_TRUE(ecs::comment_with_context(comment, hunk, "merge").revision.starts_with("second; hunk "));
    navigation::open(repo, reading::review("other"));
    ASSERT_TRUE(ecs::selected_commit_parent(repo).empty());
    navigation::step(repo, -1);
    ASSERT_EQ(ecs::selected_commit_parent(repo), "second");
    navigation::open(repo, reading::review("merge", ""));
    ASSERT_TRUE(ecs::selected_commit_parent(repo).empty());
    ASSERT_EQ(ecs::commit_review_scope(repo), "merge");
    ASSERT_EQ(diff_revisions("merge").first, "merge^");
}

TEST(revision_ranges_require_explicit_two_dot_endpoints) {
    auto range = parse_revision_range("base..topic");
    ASSERT_TRUE(range.has_value());
    ASSERT_EQ(range->first, "base");
    ASSERT_EQ(range->second, "topic");
    for (const auto& invalid : {"", "topic", "..topic", "base..", "base...topic", "a..b..c"})
        ASSERT_FALSE(parse_revision_range(invalid).has_value());
}

int main() { RUN_ALL_TESTS(); }

#include "test_framework.h"
#include "../../src/util/file_query.h"
#include "../../src/ecs/components.h"
#include "../../src/ui/code_highlight.h"
#include "../../src/util/fuzzy_match.h"
#include "../../src/util/file_tree.h"
#include "../../src/util/diff_revisions.h"
#include "../../src/util/commit_graph.h"
#include "../../src/util/review_selection.h"
#include "../../src/util/navigation.h"
#include "../../src/util/change_navigation.h"
#include "../../src/util/code_position.h"
#include "../../src/util/code_words.h"
#include "../../src/util/code_motion.h"
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

TEST(quick_open_ranks_filename_matches_before_directory_matches) {
    auto paths = fuzzy::rank({"app/z.cpp", "very/long/path/app.cpp", "src/application.cpp", "app/a.cpp"}, "app");
    ASSERT_EQ(paths, (std::vector<std::string>{"very/long/path/app.cpp", "src/application.cpp", "app/a.cpp", "app/z.cpp"}));
    auto ties = fuzzy::rank({"z/app.cpp", "a/app.cpp"}, "APP");
    ASSERT_EQ(ties, (std::vector<std::string>{"a/app.cpp", "z/app.cpp"}));
    ASSERT_EQ(fuzzy::rank({"b.cpp", "a.cpp", "recent.cpp"}, "", {"missing.cpp", "recent.cpp"}),
        (std::vector<std::string>{"recent.cpp", "a.cpp", "b.cpp"}));
}

TEST(quick_open_highlights_the_ranked_match_without_splitting_utf8) {
    ASSERT_EQ(fuzzy::matched_ranges("app", "app/app.cpp"), (std::vector<fuzzy::Range>{{4, 5}, {5, 6}, {6, 7}}));
    ASSERT_EQ(fuzzy::matched_ranges("sacp", "src/app.cpp"), (std::vector<fuzzy::Range>{{0, 1}, {4, 5}, {8, 9}, {9, 10}}));
    ASSERT_EQ(fuzzy::matched_ranges("日本", "src/日本.cpp"), (std::vector<fuzzy::Range>{{4, 7}, {7, 10}}));
    ASSERT_TRUE(fuzzy::matched_ranges("日本", "src/日語.cpp").empty());
    ASSERT_FALSE(fuzzy::score("日本", "src/日語.cpp").has_value());
}

TEST(file_query_prefers_exact_colon_paths_and_parses_positive_positions) {
    const std::vector<std::string> paths{"a.cpp", "literal:12", "report:year.cpp"};
    auto exact = file_query::parse("literal:12", paths);
    ASSERT_EQ(exact.path, std::string("literal:12")); ASSERT_FALSE(exact.position.has_value());
    auto nested = file_query::parse("literal:12:3", paths);
    ASSERT_EQ(nested.path, std::string("literal:12")); ASSERT_EQ(nested.position->line, 3);
    auto query = file_query::parse("a.cpp:120:8", paths);
    ASSERT_EQ(query.path, std::string("a.cpp")); ASSERT_EQ(query.position->line, 120); ASSERT_EQ(query.position->column, 8);
    ASSERT_EQ(file_query::parse("report:year.cpp:4:2", paths).path, std::string("report:year.cpp"));
    for (const auto* text : {"a.cpp:0", "a.cpp:-1", "a.cpp:3:0", "a.cpp:2147483648", "a.cpp:2:", ":12", "a.cpp:bad"})
        ASSERT_FALSE(file_query::parse(text, paths).error.empty());
    ASSERT_TRUE(file_query::line("").error.empty());
    ASSERT_EQ(file_query::line("12:4").position->column, 4);
    ASSERT_FALSE(file_query::line("12:4:5").error.empty());
}

TEST(find_state_belongs_to_document_and_survives_close_reopen) {
    ecs::RepoComponent repo;
    repo.repoPath = "fixture";
    navigation::open(repo, reading::source("a.cpp"), {}, reading::OpenMode::Keep);
    auto a = repo.workspace().active_id();
    navigation::open_find(repo, "alpha");
    navigation::find(repo).index = 2;
    navigation::find(repo).position = reading::ReadingAnchor{"a.cpp", "", reading::DiffSide::After, 40, 8, .15f, ' '};
    navigation::open(repo, reading::source("b.cpp"), {}, reading::OpenMode::Keep);
    ASSERT_FALSE(navigation::find(repo).open);
    navigation::open_find(repo, "beta");
    navigation::activate(repo, a);
    ASSERT_EQ(navigation::find(repo).query, "alpha");
    ASSERT_EQ(navigation::find(repo).index, 2u);
    ASSERT_EQ(navigation::find(repo).position->line, 40);
    navigation::close_find(repo);
    ASSERT_FALSE(navigation::find(repo).open);
    ASSERT_EQ(repo.readingFocus->region, reading::focus::Region::Code);
    navigation::close(repo, a);
    navigation::reopen_closed(repo);
    ASSERT_EQ(navigation::find(repo).query, "alpha");
    ASSERT_EQ(navigation::find(repo).position->column, 8);
    navigation::open_find(repo);
    ASSERT_EQ(navigation::find(repo).index, 2u);
    ASSERT_FALSE(navigation::find(repo).navigate);
}

TEST(find_matches_keep_byte_and_decoded_columns) {
    ecs::FileDiff file;
    file.filePath = "unicode.cpp";
    file.hunks.push_back({1, 1, 1, 1, "", {"+éλ needle needle"}});
    auto matches = ecs::find_diff_matches({file}, "needle");
    ASSERT_EQ(matches.size(), 2u);
    ASSERT_EQ(matches[0].column, 5u);
    ASSERT_EQ(matches[0].logicalColumn, 4);
    ASSERT_EQ(matches[1].column, 12u);
    ASSERT_EQ(matches[1].logicalColumn, 11);
}

TEST(prepared_display_offsets_match_rendered_utf8_bytes) {
    const std::string text = "\téλ  value\r";
    for (const bool visible : {false, true}) {
        ASSERT_EQ(code_highlight::display_size(text, visible), code_highlight::display_text(text, visible).size());
        size_t offset = 0;
        for (const auto part : {std::string_view(text).substr(0, 3), std::string_view(text).substr(3)})
            offset += code_highlight::display_size(part, visible);
        ASSERT_EQ(offset, code_highlight::display_text(text, visible).size());
    }
}

TEST(change_navigation_crosses_files_and_preserves_deleted_side) {
    ecs::FileDiff first, renamed, binary;
    first.filePath = "a.cpp";
    first.hunks = {{7, 7, 7, 7, "first", {" context", "-old", "+new"}},
                   {77, 7, 77, 7, "second", {" context", "-old", "+new"}}};
    renamed.filePath = "new.cpp";
    renamed.oldPath = "old.cpp";
    renamed.isRenamed = true;
    renamed.hunks = {{20, 2, 20, 0, "deleted", {"-first", "-second"}}};
    binary.filePath = "image.png";
    binary.isBinary = true;
    const std::vector<ecs::FileDiff> files{first, renamed, binary};
    const auto changes = reading::change_locations(files, {0, 1, 2}, "revision");
    ASSERT_EQ(changes.size(), size_t{4});
    ASSERT_EQ(changes[0].anchor.line, 8);
    ASSERT_EQ(changes[2].anchor.path, std::string("new.cpp"));
    ASSERT_EQ(changes[2].anchor.line, 20);
    ASSERT_EQ(changes[2].anchor.side, reading::DiffSide::Before);
    ASSERT_FALSE(changes[3].hunk.has_value());
    ASSERT_EQ(reading::adjacent_change(changes, files, changes[0].anchor, "a.cpp", 1), std::optional<size_t>{1});
    ASSERT_EQ(reading::adjacent_change(changes, files, changes[1].anchor, "a.cpp", 1), std::optional<size_t>{2});
    ASSERT_EQ(reading::adjacent_change(changes, files, changes[2].anchor, "new.cpp", -1), std::optional<size_t>{1});
    ASSERT_FALSE(reading::adjacent_change(changes, files, changes[0].anchor, "a.cpp", -1));
    ASSERT_FALSE(reading::adjacent_change(changes, files, changes[3].anchor, "image.png", 1));
    auto between = changes[0].anchor;
    between.line = 40;
    ASSERT_EQ(reading::adjacent_change(changes, files, between, "a.cpp", 1), std::optional<size_t>{1});
    ASSERT_EQ(reading::adjacent_change(changes, files, between, "a.cpp", -1), std::optional<size_t>{0});
    ASSERT_FALSE(reading::adjacent_change({}, {}, {}, "", 1));
}

TEST(review_file_navigation_drops_the_previous_files_anchor) {
    ecs::RepoComponent repo;
    reading::ReadingAnchor point{"a.cpp", "wt", reading::DiffSide::After, 80, 1, .1f, '+'};
    navigation::open(repo, reading::review("wt", "a.cpp"), {}, reading::OpenMode::Keep, point);
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->anchor.has_value());
    navigation::open(repo, reading::review("wt", "z.bin"));
    ASSERT_FALSE(repo.workspace().document(repo.workspace().active_id())->anchor.has_value());
    navigation::step(repo, -1);
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->anchor, std::optional{point});
    navigation::open(repo, reading::review("wt"));
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->anchor, std::optional{point});
}

TEST(context_state_cancels_superseded_reads_and_releases_closed_documents) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::review("wt", "a.cpp"), {}, reading::OpenMode::Keep);
    navigation::expand_hunk_context(repo, "a.cpp\nabove");
    repo.hunkContext.entries["a.cpp\nabove"].result.lines.lines = {" context"};
    navigation::open(repo, reading::review("wt", "b.cpp"));
    ASSERT_EQ(repo.hunkContext.entries.size(), 1u);
    std::promise<ecs::HunkContextResult> promise;
    std::stop_source stop;
    repo.hunkContext.future = {promise.get_future(), stop};
    navigation::expand_hunk_context(repo, "a.cpp\nabove");
    ASSERT_TRUE(stop.stop_requested());
    ASSERT_FALSE(repo.hunkContext.future.valid());
    navigation::open(repo, reading::source("a.cpp"));
    ASSERT_TRUE(repo.hunkContext.entries.empty());
    navigation::activate(repo, reading::Slot::Review);
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->contextLines.at("a.cpp\nabove"), 40);
    repo.hunkContext.entries["a.cpp\nabove"].result.lines.lines = {" context"};
    navigation::reset(repo);
    ASSERT_TRUE(repo.hunkContext.entries.empty());
    ASSERT_TRUE(repo.workspace().document(repo.workspace().active_id())->contextLines.empty());
}

TEST(caret_positions_use_logical_columns_and_one_wrapped_fragment) {
    reading::CodePosition point{"a.cpp", reading::DiffSide::After, 12, 4};
    ASSERT_EQ(reading::caret_byte(point, "a.cpp", reading::DiffSide::After, 12, "éλ ab", 1, false), std::optional<size_t>{5});
    point.column = 6;
    ASSERT_FALSE(reading::caret_byte(point, "a.cpp", reading::DiffSide::After, 12, "éλ ab", 1, false));
    ASSERT_EQ(reading::caret_byte(point, "a.cpp", reading::DiffSide::After, 12, "cde", 6, true), std::optional<size_t>{0});
    point.column = 9;
    ASSERT_EQ(reading::caret_byte(point, "a.cpp", reading::DiffSide::After, 12, "cde", 6, true), std::optional<size_t>{3});
    point.column = 10;
    ASSERT_FALSE(reading::caret_byte(point, "a.cpp", reading::DiffSide::After, 12, "cde", 6, true));
    point.column = 1;
    ASSERT_EQ(reading::caret_byte(point, "a.cpp", reading::DiffSide::After, 12, "", 1, true), std::optional<size_t>{0});
    ASSERT_FALSE(reading::caret_byte(point, "b.cpp", reading::DiffSide::After, 12, "", 1, true));
    ASSERT_FALSE(reading::caret_byte(point, "a.cpp", reading::DiffSide::Before, 12, "", 1, true));
}

TEST(caret_belongs_to_each_document_and_tracks_explicit_line_destinations) {
    ecs::RepoComponent repo;
    navigation::open(repo, reading::source("a.cpp", "", 12));
    navigation::set_caret(repo, {"a.cpp", reading::DiffSide::After, 13, 5});
    const auto id = repo.workspace().active_id();
    navigation::keep(repo, id);
    navigation::open(repo, reading::source("b.cpp", "", 9));
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->caret->line, 9);
    navigation::activate(repo, id);
    ASSERT_EQ(repo.workspace().document(id)->caret->line, 13);
    navigation::set_caret(repo, {"a.cpp", reading::DiffSide::After, 13, 5});
    navigation::clear_source_reveal(repo);
    navigation::open(repo, reading::source("b.cpp"));
    navigation::activate(repo, id);
    ASSERT_EQ(repo.workspace().document(id)->caret->line, 13);
    ASSERT_EQ(repo.workspace().document(id)->caret->column, 5);
}

TEST(code_words_preserve_identifiers_unicode_and_composed_characters) {
    const std::string text = "a_éλ é 👨‍👩‍👧‍👦 ;";
    auto selected = [&](size_t byte) {
        auto [first, last] = reading::word_at(text, byte);
        return text.substr(first, last - first);
    };
    ASSERT_EQ(selected(2), "a_éλ");
    ASSERT_EQ(selected(4), "a_éλ");
    ASSERT_EQ(selected(text.find("é") + 2), "é");
    ASSERT_EQ(selected(text.find("👨") + 5), "👨‍👩‍👧‍👦");
    ASSERT_EQ(selected(text.size()), ";");
    ASSERT_EQ(reading::word_at("", 99), (std::pair<size_t, size_t>{0, 0}));
}

TEST(code_words_group_whitespace_without_swallowing_punctuation) {
    const std::string text = "one\t  two->three";
    auto [first, last] = reading::word_at(text, 4);
    ASSERT_EQ(text.substr(first, last - first), "\t  ");
    auto [a, b] = reading::word_at(text, 9);
    ASSERT_EQ(text.substr(a, b - a), "-");
}

TEST(code_motion_keeps_composed_characters_whole_and_crosses_lines) {
    std::vector<reading::CodeLine> lines{{1, 1, "aé é 🙂"}, {2, 1, "short"}};
    reading::CodePosition point{"a.cpp", reading::DiffSide::After, 1, 4};
    point = reading::move_code(point, reading::CodeMotion::Right, lines);
    ASSERT_EQ(point.column, 6);
    point = reading::move_code(point, reading::CodeMotion::Left, lines);
    ASSERT_EQ(point.column, 4);
    point = reading::move_code(point, reading::CodeMotion::LineEnd, lines);
    ASSERT_EQ(point.column, 8);
    point = reading::move_code(point, reading::CodeMotion::Right, lines);
    ASSERT_EQ(point.line, 2);
    ASSERT_EQ(point.column, 1);
    point = reading::move_code(point, reading::CodeMotion::Left, lines);
    ASSERT_EQ(point.line, 1);
    ASSERT_EQ(point.column, 8);
}

TEST(code_motion_word_and_document_boundaries_preserve_identity) {
    std::vector<reading::CodeLine> lines{{20, 50, "alpha_éλ beta"}, {22, 1, ""}, {30, 1, "end"}};
    reading::CodePosition point{"old.cpp", reading::DiffSide::Before, 20, 50};
    point = reading::move_code(point, reading::CodeMotion::WordRight, lines);
    ASSERT_EQ(point.column, 58);
    point = reading::move_code(point, reading::CodeMotion::WordRight, lines);
    ASSERT_EQ(point.column, 63);
    point = reading::move_code(point, reading::CodeMotion::WordLeft, lines);
    ASSERT_EQ(point.column, 59);
    point = reading::move_code(point, reading::CodeMotion::Down, lines);
    ASSERT_EQ(point.line, 22);
    ASSERT_EQ(point.column, 1);
    point = reading::move_code(point, reading::CodeMotion::DocumentEnd, lines);
    ASSERT_EQ(point.line, 30);
    ASSERT_EQ(point.column, 4);
    ASSERT_EQ(point.path, "old.cpp");
    ASSERT_EQ(point.side, reading::DiffSide::Before);
    point = reading::move_code(point, reading::CodeMotion::DocumentStart, lines);
    ASSERT_EQ(point.line, 20);
    ASSERT_EQ(point.column, 50);
    ASSERT_EQ(reading::move_code(point, reading::CodeMotion::Left, lines), point);
}

TEST(caret_reveal_does_not_append_visits_and_page_requests_cancel_on_navigation) {
    ecs::RepoComponent repo;
    repo.repoPath = "fixture";
    navigation::open(repo, reading::source("a.cpp"), {}, reading::OpenMode::Keep);
    const auto visits = repo.workspace().history().size();
    reading::CodePosition point{"a.cpp", reading::DiffSide::After, 5000, 8};
    navigation::reveal_caret(repo, point, .8f);
    ASSERT_EQ(repo.workspace().history().size(), visits);
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->caret, std::optional{point});
    ASSERT_EQ(repo.fullFileTargetLine(), 5000);
    repo.fullFilePage.sourceIdentity = "version-one";
    navigation::request_caret_page(repo, {ecs::FilePageRequest::Action::Previous, {90000}}, point, reading::CodeMotion::DocumentEnd);
    ASSERT_EQ(repo.fullFilePageRequest.sourceIdentity, "version-one");
    ASSERT_TRUE(repo.pendingCaret.has_value());
    ASSERT_EQ(repo.pendingCaretMotion, std::optional{reading::CodeMotion::DocumentEnd});
    auto stamp = navigation::stamp(repo, "pending-page");
    navigation::open(repo, reading::source("b.cpp"));
    ASSERT_FALSE(repo.pendingCaret.has_value());
    ASSERT_FALSE(repo.pendingCaretMotion.has_value());
    ASSERT_FALSE(navigation::accepts(repo, stamp, "pending-page"));
}

TEST(selection_survives_closing_and_cancels_superseded_copy_work) {
    ecs::RepoComponent repo;
    repo.repoPath = "fixture";
    navigation::open(repo, reading::source("a.cpp"), {}, reading::OpenMode::Keep);
    reading::CodeSelection selection{{"a.cpp", reading::DiffSide::After, 1, 2},
        {"a.cpp", reading::DiffSide::After, 5000, 9}, {"a.cpp", reading::WorkingTree{}}, "version"};
    navigation::set_selection(repo, selection);
    std::promise<ecs::SelectionCopyResult> promise;
    std::stop_source stop;
    repo.selectionCopy.future = {promise.get_future(), stop};
    navigation::set_selection(repo, selection);
    ASSERT_FALSE(stop.stop_requested());
    selection.head.line = 6000;
    navigation::set_selection(repo, selection);
    ASSERT_TRUE(stop.stop_requested());
    ASSERT_FALSE(repo.selectionCopy.future.valid());
    const auto id = repo.workspace().active_id();
    navigation::close(repo, id);
    navigation::reopen_closed(repo);
    ASSERT_EQ(repo.workspace().document(repo.workspace().active_id())->selection, std::optional{selection});
    navigation::set_selection(repo, {});
    ASSERT_FALSE(repo.workspace().document(repo.workspace().active_id())->selection.has_value());
}

int main() { RUN_ALL_TESTS(); }

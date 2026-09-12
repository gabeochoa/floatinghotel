#include "test_framework.h"
#include "../../src/git/repository_search.h"
#include "../../src/util/path_glob.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST(repository_search_arguments_keep_tree_index_and_working_tree_distinct) {
    auto working = git::repository_search_args({"repo", "", "needle"});
    ASSERT_TRUE(std::find(working.begin(), working.end(), "--untracked") != working.end());
    auto index = git::repository_search_args({"repo", "INDEX", "needle"});
    ASSERT_TRUE(std::find(index.begin(), index.end(), "--cached") != index.end());
    ASSERT_TRUE(std::find(index.begin(), index.end(), "--untracked") == index.end());
    auto historical = git::repository_search_args({"repo", "abc123", "needle"});
    ASSERT_EQ(historical[historical.size() - 2], "abc123");
    ASSERT_EQ(historical.back(), "--");
}

TEST(repository_search_removes_only_the_known_revision_prefix) {
    constexpr char output[] = "abc123:odd:name.cpp\0" "12\0" "needle\n";
    auto matches = git::parse_search_matches(std::string(output, sizeof(output) - 1), "abc123");
    ASSERT_EQ(matches.size(), 1u);
    ASSERT_EQ(matches[0].file, "odd:name.cpp");
    ASSERT_EQ(matches[0].revision, "abc123");
    auto working = git::parse_search_matches(std::string(output, sizeof(output) - 1), "");
    ASSERT_EQ(working[0].file, "abc123:odd:name.cpp");
}

TEST(changed_file_search_keeps_paths_literal_and_empty_scope_empty) {
    ecs::SearchQuery query{"", "", "needle", true};
    auto empty = git::search_repository_async(query).get();
    ASSERT_TRUE(empty.error.empty());
    ASSERT_TRUE(empty.matches.empty());
    query.paths = {"src/[literal].cpp", "odd:name.cpp"};
    auto args = git::repository_search_args(query);
    ASSERT_EQ(args[args.size() - 2], ":(literal)src/[literal].cpp");
    ASSERT_EQ(args.back(), ":(literal)odd:name.cpp");
}

TEST(repository_search_matching_flags_are_explicit_and_composable) {
    ecs::SearchQuery query{"repo", "", "needle|other"};
    auto literal = git::repository_search_args(query);
    ASSERT_TRUE(std::find(literal.begin(), literal.end(), "-F") != literal.end());
    query.matching = {true, false, true};
    auto regex = git::repository_search_args(query);
    for (const std::string flag : {"-E", "-i", "-w"})
        ASSERT_TRUE(std::find(regex.begin(), regex.end(), flag) != regex.end());
    ASSERT_TRUE(std::find(regex.begin(), regex.end(), "-F") == regex.end());
    ASSERT_TRUE(std::find(regex.begin(), regex.end(), "needle|other") != regex.end());
}

TEST(search_globs_support_nested_paths_and_do_not_broaden_changed_scope) {
    ASSERT_TRUE(path_glob_matches("**/*.cpp", "main.cpp"));
    ASSERT_TRUE(path_glob_matches("**/*.cpp", "src/deep/main.cpp"));
    ASSERT_FALSE(path_glob_matches("*.cpp", "src/main.cpp"));
    ASSERT_TRUE(path_glob_matches("src/[ab]?.cpp", "src/a1.cpp"));
    ecs::SearchQuery query{"", "", "needle"};
    query.includeGlob = "**/*.cpp";
    query.excludeGlob = "**/test*";
    auto args = git::repository_search_args(query);
    ASSERT_EQ(args[args.size() - 2], ":(glob)**/*.cpp");
    ASSERT_EQ(args.back(), ":(glob,exclude)**/test*");
    query.changedOnly = true;
    query.paths = {"notes.txt"};
    auto result = git::search_repository_async(query).get();
    ASSERT_TRUE(result.matches.empty());
    ASSERT_TRUE(result.error.empty());
}

TEST(search_preview_preserves_line_numbers_revision_and_detects_stale_results) {
    auto preview = git::search_preview_lines("before\nneedle\nafter\nlast\n", {"app.cpp", 2, "needle", "abc123"});
    ASSERT_EQ(preview.match.revision, "abc123");
    ASSERT_EQ(preview.lines.size(), 4u);
    ASSERT_EQ(preview.lines.front().first, 1);
    ASSERT_EQ(preview.lines.back().first, 4);
    ASSERT_FALSE(preview.changedSinceSearch);
    ASSERT_TRUE(git::search_preview_lines("changed\n", {"app.cpp", 1, "needle"}).changedSinceSearch);
    ASSERT_FALSE(git::search_preview_lines("one\n", {"app.cpp", 5, "needle"}).error.empty());
}

TEST(search_preview_caps_working_and_historical_reads_and_does_not_follow_symlinks) {
    char pattern[] = "/tmp/fh-preview-unit.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    std::filesystem::path path(directory);
    { std::ofstream file(path / "large"); file << std::string(1024 * 1024 + 1, 'x'); }
    { std::ofstream file(path / "small"); file << "before\nneedle\nafter\n"; }
    std::filesystem::create_symlink("small", path / "link");
    auto large = git::search_preview_async(directory, {"large", 1, "x"}).get();
    ASSERT_TRUE(large.error.find("1 MiB") != std::string::npos);
    auto link = git::search_preview_async(directory, {"link", 1, "small"}).get();
    ASSERT_EQ(link.lines[0].second, "small");
    ASSERT_TRUE(git::git_run(directory, {"init", "-q"}).success());
    ASSERT_TRUE(git::git_run(directory, {"add", "."}).success());
    ASSERT_TRUE(git::git_run(directory, {"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "baseline"}).success());
    auto historic = git::search_preview_async(directory, {"large", 1, "x", "HEAD"}).get();
    ASSERT_TRUE(historic.error.find("1 MiB") != std::string::npos);
    auto index = git::search_preview_async(directory, {"small", 2, "needle", "INDEX"}).get();
    ASSERT_EQ(index.lines[2].second, "after");
    std::filesystem::remove_all(path);
}

int main() { RUN_ALL_TESTS(); }

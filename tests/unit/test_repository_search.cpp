#include "test_framework.h"
#include "../../src/git/repository_search.h"
#include "../../src/util/path_glob.h"

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

int main() { RUN_ALL_TESTS(); }

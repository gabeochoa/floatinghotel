#include "test_framework.h"
#include "../../src/util/document_titles.h"

using namespace reading;

static Document file(unsigned id, std::string path, std::string revision = {}) {
    return {{id}, source(std::move(path), std::move(revision))};
}

static Document commit(unsigned id, std::string oid, std::string subject, std::string parent = {}) {
    Document document{{id}, review(parent.empty() ? oid : "parent:" + parent + ":" + oid)};
    document.subject = std::move(subject);
    return document;
}

TEST(unique_file_keeps_its_simple_name) {
    auto title = document_titles({file(1, "src/main.cpp")})[0];
    ASSERT_STREQ(title.label, "main.cpp");
    ASSERT_TRUE(title.badge.empty());
    ASSERT_STREQ(title.tooltip, "src/main.cpp @ working tree");
}

TEST(duplicate_files_use_shortest_distinguishing_parent_suffix) {
    auto titles = document_titles({file(1, "client/src/main.cpp"), file(2, "server/src/main.cpp"), file(3, "tests/main.cpp")});
    ASSERT_STREQ(titles[0].label, "main.cpp · client/src");
    ASSERT_STREQ(titles[1].label, "main.cpp · server/src");
    ASSERT_STREQ(titles[2].label, "main.cpp · tests");
}

TEST(root_and_unicode_paths_remain_distinguishable) {
    auto titles = document_titles({file(1, "読み方.md"), file(2, "資料/読み方.md")});
    ASSERT_STREQ(titles[0].label, "読み方.md · .");
    ASSERT_STREQ(titles[1].label, "読み方.md · 資料");
}

TEST(same_file_at_multiple_revisions_uses_badges_without_redundant_parents) {
    auto oid = std::string(40, 'a');
    auto titles = document_titles({file(1, "src/main.cpp"), file(2, "src/main.cpp", "INDEX"), file(3, "src/main.cpp", oid)});
    for (const auto& title : titles) ASSERT_STREQ(title.label, "main.cpp");
    ASSERT_STREQ(titles[0].badge, "Working tree");
    ASSERT_STREQ(titles[1].badge, "Index");
    ASSERT_STREQ(titles[2].badge, "aaaaaaa");
    ASSERT_STREQ(titles[2].tooltip, "src/main.cpp @ " + oid);
}

TEST(short_hashes_extend_only_to_remove_collisions) {
    auto a = std::string(40, 'a');
    auto b = a; b[8] = 'b';
    auto titles = document_titles({file(1, "a.cpp", a), file(2, "a.cpp", b), file(3, "b.cpp", a)});
    ASSERT_STREQ(titles[0].badge, "aaaaaaaaa");
    ASSERT_STREQ(titles[1].badge, "aaaaaaaab");
    ASSERT_STREQ(titles[2].badge, titles[0].badge);
}

TEST(parent_disambiguation_ignores_duplicate_copies_of_the_same_path) {
    auto titles = document_titles({file(1, "client/src/a.cpp"), file(2, "client/src/a.cpp", "HEAD"), file(3, "server/src/a.cpp")});
    ASSERT_STREQ(titles[0].label, "a.cpp · client/src");
    ASSERT_STREQ(titles[1].label, titles[0].label);
    ASSERT_STREQ(titles[2].label, "a.cpp · server/src");
}

TEST(commit_subject_and_full_identity_are_retained) {
    auto oid = std::string(40, 'a');
    auto title = document_titles({commit(1, oid, "Keep my place")})[0];
    ASSERT_STREQ(title.label, "Keep my place");
    ASSERT_TRUE(title.badge.empty());
    ASSERT_STREQ(title.tooltip, "Keep my place\nCommit " + oid + "\nFirst parent");
}

TEST(duplicate_commit_subjects_show_distinct_hashes) {
    auto titles = document_titles({commit(1, std::string(40, 'a'), "Fix"), commit(2, std::string(40, 'b'), "Fix")});
    ASSERT_STREQ(titles[0].badge, "aaaaaaa");
    ASSERT_STREQ(titles[1].badge, "bbbbbbb");
}

TEST(merge_parent_reviews_remain_distinguishable) {
    auto oid = std::string(40, 'a');
    auto titles = document_titles({commit(1, oid, "Merge"), commit(2, oid, "Merge", std::string(40, 'b'))});
    ASSERT_STREQ(titles[0].badge, "aaaaaaa · first parent");
    ASSERT_STREQ(titles[1].badge, "aaaaaaa · from bbbbbbb");
    ASSERT_TRUE(titles[1].tooltip.ends_with("Parent " + std::string(40, 'b')));
}

TEST(unresolved_commits_and_comparisons_show_exact_destination) {
    ASSERT_STREQ(document_titles({commit(1, "missing-branch", "")})[0].label, "Commit missing-branch");
    auto titles = document_titles({Document{{1}, review("compare:HEAD~1:HEAD")}, Document{{2}, review("wt")}, Document{{3}, review("index")}});
    ASSERT_STREQ(titles[0].label, "HEAD~1 → HEAD");
    ASSERT_STREQ(titles[1].label, "Unstaged changes");
    ASSERT_STREQ(titles[2].label, "Staged changes");
}

int main() { RUN_ALL_TESTS(); }

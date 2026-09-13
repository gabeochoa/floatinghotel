#include "test_framework.h"
#include "../../src/git/path_list.h"
#include <filesystem>
#include <fstream>
#include <unistd.h>

TEST(default_scope_uses_source_or_review_after_revision) {
    const std::string old(40, 'a'), current(40, 'b');
    ASSERT_TRUE(std::holds_alternative<reading::WorkingTree>(reading::source_revision_for(reading::review("wt"))));
    ASSERT_TRUE(std::holds_alternative<reading::Index>(reading::source_revision_for(reading::review("index"))));
    ASSERT_TRUE(std::holds_alternative<reading::Index>(reading::source_revision_for(reading::source("a", "INDEX"))));
    for (const auto& location : std::vector<reading::Location>{reading::source("a", current), reading::review(current),
            reading::review("parent:" + old + ":" + current), reading::review("compare:" + old + ":" + current)}) {
        const auto revision = reading::source_revision_for(location);
        ASSERT_TRUE(std::holds_alternative<reading::ObjectId>(revision));
        ASSERT_EQ(reading::revision_text(revision), current);
    }
    ASSERT_TRUE(std::holds_alternative<reading::RevisionQuery>(reading::source_revision_for(reading::review("HEAD~1"))));
}

TEST(path_lists_preserve_deleted_renamed_unicode_and_index_files) {
    char directory[] = "/tmp/fh-path-list.XXXXXX";
    auto* repo = mkdtemp(directory);
    ASSERT_TRUE(repo != nullptr);
    auto git = [&](std::vector<std::string> args) { auto result = git::git_run(repo, args); ASSERT_TRUE(result.success()); return result.stdout_str(); };
    git({"init", "-q", "-b", "main"});
    git({"config", "user.name", "Path fixture"});
    git({"config", "user.email", "paths@example.invalid"});
    git({"config", "commit.gpgsign", "false"});
    for (const auto* name : {"old.cpp", "gone.cpp", "日本語.cpp", "line\nbreak.cpp"})
        std::ofstream(std::filesystem::path(repo) / name) << "old contents\n";
    git({"add", "."}); git({"commit", "-qm", "Original"});
    auto old = git({"rev-parse", "HEAD"}); old.pop_back();
    git({"mv", "old.cpp", "new.cpp"}); git({"rm", "gone.cpp"}); git({"commit", "-qm", "Rename and delete"});
    std::ofstream(std::filesystem::path(repo) / "staged.cpp") << "index contents\n";
    git({"add", "staged.cpp"});
    std::ofstream(std::filesystem::path(repo) / "untracked.cpp") << "working contents\n";
    const auto original = git::read_paths(repo, reading::RevisionQuery{"HEAD~1"});
    ASSERT_TRUE(original.error.empty());
    ASSERT_EQ(reading::revision_text(original.revision), old);
    ASSERT_EQ(original.paths, (std::vector<std::string>{"gone.cpp", "line\nbreak.cpp", "old.cpp", "日本語.cpp"}));
    const auto head = git::read_paths(repo, reading::RevisionQuery{"HEAD"});
    ASSERT_TRUE(head.error.empty());
    ASSERT_TRUE(std::find(head.paths.begin(), head.paths.end(), "new.cpp") != head.paths.end());
    ASSERT_TRUE(std::find(head.paths.begin(), head.paths.end(), "old.cpp") == head.paths.end());
    const auto index = git::read_paths(repo, reading::Index{});
    ASSERT_TRUE(index.error.empty());
    ASSERT_TRUE(std::find(index.paths.begin(), index.paths.end(), "staged.cpp") != index.paths.end());
    ASSERT_TRUE(std::find(index.paths.begin(), index.paths.end(), "untracked.cpp") == index.paths.end());
    const auto working = git::read_paths(repo, reading::WorkingTree{});
    ASSERT_TRUE(working.error.empty());
    ASSERT_TRUE(std::find(working.paths.begin(), working.paths.end(), "untracked.cpp") != working.paths.end());
    const auto missing = git::read_paths(repo, reading::ObjectId{std::string(40, 'f')});
    ASSERT_FALSE(missing.error.empty()); ASSERT_TRUE(missing.paths.empty());
    const auto bounded = git::read_paths(repo, reading::ObjectId{old}, {}, 20);
    ASSERT_TRUE(bounded.error.empty()); ASSERT_TRUE(bounded.truncated);
    for (const auto& path : bounded.paths)
        ASSERT_TRUE(std::find(original.paths.begin(), original.paths.end(), path) != original.paths.end());
    std::stop_source stop; stop.request_stop();
    const auto cancelled = git::read_paths(repo, reading::ObjectId{old}, stop.get_token());
    ASSERT_FALSE(cancelled.error.empty()); ASSERT_TRUE(cancelled.paths.empty());
    std::filesystem::remove_all(repo);
}

int main() { RUN_ALL_TESTS(); }

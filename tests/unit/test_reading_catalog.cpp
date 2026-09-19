#include "test_framework.h"
#include "../../src/git/reading_catalog.h"
#include "../../src/git/push_destination.h"
#include "../../src/git/history_scope.h"
#include <filesystem>
#include <fstream>

namespace {
struct Fixture {
    std::string path;
    Fixture() {
        char pattern[] = "/tmp/floatinghotel-catalog-XXXXXX";
        path = std::filesystem::canonical(mkdtemp(pattern)).string();
        run({"init", "-b", "main"});
        run({"config", "user.email", "fixture@example.test"});
        run({"config", "user.name", "Fixture Reader"});
        run({"config", "commit.gpgsign", "false"});
        std::ofstream(path + "/read me.txt") << "first\n";
        run({"add", "."});
        run({"commit", "-m", "Initial reader"});
    }
    ~Fixture() { std::filesystem::remove_all(path); }
    git::GitResult run(std::vector<std::string> args) {
        auto result = git::git_run(path, args);
        ASSERT_TRUE(result.success());
        return result;
    }
};
}

TEST(catalog_resolves_refs_without_checking_out_and_finds_other_branch_commits) {
    Fixture fixture;
    const auto first = git::catalog::trim_line(fixture.run({"rev-parse", "HEAD"}).stdout_str());
    fixture.run({"tag", "-a", "v1", "-m", "Release one"});
    fixture.run({"branch", "topic"});
    fixture.run({"update-ref", "refs/remotes/upstream/topic", first});
    auto local = git::catalog::refs(fixture.path, false, {});
    auto remote = git::catalog::refs(fixture.path, true, {});
    ASSERT_TRUE(local.error.empty());
    ASSERT_EQ(local.entries.size(), 3u);
    ASSERT_EQ(remote.entries.size(), 1u);
    auto tags = git::catalog::filter(local.entries, git::catalog::Kind::Tag, "v1");
    ASSERT_EQ(tags.size(), 1u);
    ASSERT_EQ(tags.front().object, first);
    ASSERT_TRUE(remote.entries.front().remote);
    ASSERT_EQ(remote.entries.front().title, std::string("upstream/topic"));
    auto exact = git::catalog::commits(fixture.path, first.substr(0, 10), {});
    ASSERT_EQ(exact.entries.size(), 1u);
    ASSERT_EQ(exact.entries.front().object, first);
    fixture.run({"checkout", "topic"});
    fixture.run({"commit", "--allow-empty", "-m", "Remote reader idea"});
    fixture.run({"checkout", "main"});
    auto found = git::catalog::commits(fixture.path, "reader idea", {});
    ASSERT_EQ(found.entries.size(), 1u);
    ASSERT_EQ(git::catalog::trim_line(fixture.run({"branch", "--show-current"}).stdout_str()), std::string("main"));
    ASSERT_FALSE(git::catalog::commits(fixture.path, "000000000000", {}).error.empty());
}

TEST(catalog_category_filters_preserve_identity_and_input) {
    using namespace git::catalog;
    const std::vector<Entry> entries{{Kind::File, "src/topic", "src/topic", "", ""},
        {Kind::Branch, "refs/heads/topic", "topic", "", "a"}, {Kind::Tag, "refs/tags/topic", "topic", "", "b"}};
    ASSERT_EQ(filter(entries, Kind::All, "topic").size(), 3u);
    const auto filtered = filter(entries, Kind::Branch, "topic");
    ASSERT_EQ(filtered.size(), 1u);
    ASSERT_EQ(filtered[0].identity, std::string("refs/heads/topic"));
    ASSERT_EQ(entries.size(), 3u);
    ASSERT_FALSE(hash_query("not-a-hash"));
    ASSERT_TRUE(hash_query("aBcD1234"));
}

TEST(catalog_reflog_and_stashes_are_read_only) {
    Fixture fixture;
    std::ofstream(fixture.path + "/read me.txt", std::ios::app) << "draft\n";
    fixture.run({"stash", "push", "-m", "Reader draft"});
    auto stashes = git::catalog::stashes(fixture.path, {});
    ASSERT_EQ(stashes.entries.size(), 1u);
    ASSERT_TRUE(stashes.entries[0].title.find("Reader draft") != std::string::npos);
    auto reflog = git::catalog::reflog(fixture.path, 0, "Initial", {});
    ASSERT_FALSE(reflog.entries.empty());
    ASSERT_TRUE(reflog.entries[0].detail.find("Fixture Reader") != std::string::npos);
    ASSERT_TRUE(git::catalog::reflog(fixture.path, 10000, "", {}).entries.empty());
    ASSERT_EQ(git::catalog::stashes(fixture.path, {}).entries.size(), 1u);
}

TEST(git_log_boundary_redacts_without_changing_raw_results) {
    Fixture fixture;
    std::string recorded;
    git::set_log_callback([&](const auto& command, const auto& output, const auto& error, bool) {
        recorded = command + output + error;
    });
    auto result = git::git_run(fixture.path, {"-c", "test.value=https://user:fixture-secret@example.test/repo", "config", "--get", "test.value"});
    git::set_log_callback({});
    ASSERT_TRUE(result.success());
    ASSERT_TRUE(result.stdout_str().find("fixture-secret") != std::string::npos);
    ASSERT_TRUE(recorded.find("fixture-secret") == std::string::npos);
    ASSERT_TRUE(recorded.find("example.test/repo") != std::string::npos);
}

TEST(catalog_change_badges_follow_revision_and_keep_deleted_paths) {
    Fixture fixture;
    const auto root = git::catalog::trim_line(fixture.run({"rev-parse", "HEAD"}).stdout_str());
    auto original = git::catalog::changes(fixture.path, root, "", {});
    ASSERT_EQ(original.paths.at("read me.txt"), std::string("Added"));
    fixture.run({"rm", "read me.txt"});
    fixture.run({"commit", "-m", "Remove document"});
    const auto head = git::catalog::trim_line(fixture.run({"rev-parse", "HEAD"}).stdout_str());
    auto changes = git::catalog::changes(fixture.path, head, "", {});
    ASSERT_EQ(changes.paths.at("read me.txt"), std::string("Deleted"));
    ASSERT_EQ(changes.before, root);
    auto aggregate = git::git_compare_async(fixture.path, "empty", root, false, 3, false).get();
    ASSERT_TRUE(aggregate.patch.success());
    ASSERT_TRUE(aggregate.patch.stdout_str().find("+first") != std::string::npos);
}

TEST(worktree_catalog_retains_paths_and_reads_status_without_switching) {
    Fixture fixture;
    const auto child = fixture.path + "/linked space";
    fixture.run({"worktree", "add", "-b", "linked", child});
    auto page = git::catalog::worktrees(fixture.path, {});
    ASSERT_TRUE(page.error.empty());
    ASSERT_EQ(page.entries.size(), 2u);
    auto found = std::find_if(page.entries.begin(), page.entries.end(), [&](const auto& entry) { return entry.identity == child; });
    ASSERT_TRUE(found != page.entries.end());
    ASSERT_EQ(found->title, std::string("linked space · linked"));
    ASSERT_EQ(found->detail, child);
    ASSERT_EQ(git::catalog::worktree_status(child, {}), std::string("Clean"));
    std::ofstream(child + "/read me.txt", std::ios::app) << "working\n";
    ASSERT_EQ(git::catalog::worktree_status(child, {}), std::string("1 changed file"));
    ASSERT_EQ(git::catalog::trim_line(fixture.run({"branch", "--show-current"}).stdout_str()), std::string("main"));
}

TEST(push_uses_the_upstream_and_separate_push_url) {
    Fixture fixture;
    const auto fetchPath = fixture.path + "/fetch.git";
    const auto pushPath = fixture.path + "/push.git";
    fixture.run({"init", "--bare", fetchPath});
    fixture.run({"init", "--bare", pushPath});
    fixture.run({"remote", "add", "upstream", fetchPath});
    fixture.run({"remote", "set-url", "--push", "upstream", pushPath});
    fixture.run({"config", "branch.main.remote", "upstream"});
    fixture.run({"config", "branch.main.merge", "refs/heads/review"});
    const auto destination = git::read_push_destination(fixture.path, "", {});
    ASSERT_TRUE(destination.error.empty());
    ASSERT_EQ(destination.remote, std::string("upstream"));
    ASSERT_EQ(destination.destination, std::string("review"));
    ASSERT_EQ(destination.fetchUrl, fetchPath);
    ASSERT_EQ(destination.pushUrl, pushPath);
    ASSERT_TRUE(git::push_explicit(fixture.path, destination).success());
    ASSERT_TRUE(git::git_run(pushPath, {"rev-parse", "--verify", "refs/heads/review"}).success());
    ASSERT_FALSE(git::git_run(fetchPath, {"rev-parse", "--verify", "refs/heads/review"}).success());
    fixture.run({"checkout", "-b", "different"});
    ASSERT_FALSE(git::push_explicit(fixture.path, destination).success());
}

TEST(history_scope_excludes_remote_only_commits_unless_requested) {
    Fixture fixture;
    fixture.run({"checkout", "-b", "remote-only"});
    fixture.run({"commit", "--allow-empty", "-m", "Remote-only commit"});
    const auto remote = git::catalog::trim_line(fixture.run({"rev-parse", "HEAD"}).stdout_str());
    fixture.run({"checkout", "main"});
    fixture.run({"update-ref", "refs/remotes/server/topic", remote});
    fixture.run({"branch", "-D", "remote-only"});
    git::HistoryScope scope{git::HistoryScope::Mode::All, "", false};
    ASSERT_TRUE(fixture.run(git::history_scope_args(scope)).stdout_str().find(remote) == std::string::npos);
    scope.remotes = true;
    ASSERT_TRUE(fixture.run(git::history_scope_args(scope)).stdout_str().find(remote) != std::string::npos);
    ASSERT_FALSE(git::catalog::diagnostics(fixture.path, {}).entries.empty());
}

TEST(submodule_inspection_distinguishes_gitlink_and_checkout_and_missing_state) {
    Fixture fixture;
    const auto child = fixture.path + "/nested";
    fixture.run({"init", "-b", "main", child});
    ASSERT_TRUE(git::git_run(child, {"-c", "user.name=Reader", "-c", "user.email=reader@example.invalid", "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-m", "Child root"}).success());
    const auto head = git::catalog::trim_line(git::git_run(child, {"rev-parse", "HEAD"}).stdout_str());
    fixture.run({"update-index", "--add", "--cacheinfo", "160000," + head + ",nested"});
    auto modules = git::catalog::submodules(fixture.path, {});
    ASSERT_EQ(modules.entries.size(), 1u);
    ASSERT_EQ(modules.entries[0].object, head);
    ASSERT_TRUE(git::catalog::submodule_status(child, {}).find("Checkout " + head) != std::string::npos);
    std::filesystem::remove_all(child + "/.git");
    ASSERT_EQ(git::catalog::submodule_status(child, {}), std::string("Submodule is not initialized"));
    std::filesystem::remove_all(child);
    ASSERT_EQ(git::catalog::submodule_status(child, {}), std::string("Missing submodule directory"));
}

int main() { RUN_ALL_TESTS(); }

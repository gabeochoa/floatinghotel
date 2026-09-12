#include "test_framework.h"
#include "../../src/git/commit_patch.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <thread>
#include <unistd.h>

TEST(commit_patches_are_parsed_on_workers_with_resolved_revision_identity) {
    char pattern[] = "/tmp/fh-commit-patch-unit.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    std::filesystem::path path(directory);
    ASSERT_TRUE(git::git_run(directory, {"init", "-q"}).success());
    { std::ofstream file(path / "app.cpp"); file << "before\n"; }
    ASSERT_TRUE(git::git_run(directory, {"add", "."}).success());
    const std::vector<std::string> commit{"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qam", "fixture"};
    ASSERT_TRUE(git::git_run(directory, commit).success());
    auto root = git::load_commit_patch_async({directory, "HEAD"}).get();
    ASSERT_TRUE(root.error.empty());
    ASSERT_EQ(root.files.size(), 1u);
    ASSERT_TRUE(root.files[0].isNew);
    ASSERT_TRUE(root.resolvedParent.empty());
    { std::ofstream file(path / "app.cpp"); file << "after\n"; }
    ASSERT_TRUE(git::git_run(directory, commit).success());
    auto next = git::load_commit_patch_async({directory, "HEAD", root.resolvedCommit, 0}).get();
    ASSERT_TRUE(next.error.empty());
    ASSERT_EQ(next.resolvedParent, root.resolvedCommit);
    ASSERT_EQ(next.files[0].hunks[0].lines.back(), "+after");
    ASSERT_FALSE(git::read_commit_patch({directory, "HEAD", "missing-parent"}).error.empty());
    std::stop_source cancelled;
    cancelled.request_stop();
    ASSERT_TRUE(git::read_commit_patch({directory, "HEAD"}, cancelled.get_token()).error.find("cancelled") != std::string::npos);
    std::filesystem::remove_all(path);
}

TEST(commit_patch_cancellation_stops_an_active_textconv) {
    char pattern[] = "/tmp/fh-commit-patch-stop.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    std::filesystem::path path(directory);
    ASSERT_TRUE(git::git_run(directory, {"init", "-q"}).success());
    { std::ofstream file(path / ".gitattributes"); file << "slow.txt diff=slow\n"; }
    { std::ofstream file(path / "slow.txt"); file << "before\n"; }
    ASSERT_TRUE(git::git_run(directory, {"add", "."}).success());
    ASSERT_TRUE(git::git_run(directory, {"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qm", "fixture"}).success());
    auto marker = path / "started";
    auto script = path / "slow.sh";
    { std::ofstream file(script); file << "#!/bin/sh\ntouch '" << marker.string() << "'\nsleep 15\ncat \"$1\"\n"; }
    std::filesystem::permissions(script, std::filesystem::perms::owner_all);
    ASSERT_TRUE(git::git_run(directory, {"config", "diff.slow.textconv", script.string()}).success());
    auto pending = git::load_commit_patch_async({directory, "HEAD"});
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!std::filesystem::exists(marker) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    ASSERT_TRUE(std::filesystem::exists(marker));
    pending.cancel();
    ASSERT_EQ(pending.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    ASSERT_TRUE(pending.get().error.find("cancelled") != std::string::npos);
    std::filesystem::remove_all(path);
}

int main() { RUN_ALL_TESTS(); }

#include "test_framework.h"
#include "../../src/git/commit_patch.h"
#include "../../src/git/commit_patch_cache.h"
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
    size_t commands = 0;
    git::set_log_callback([&](const auto&, const auto&, const auto&, bool) { ++commands; });
    auto warm = git::read_commit_patch({directory, root.resolvedCommit});
    git::set_log_callback({});
    ASSERT_EQ(commands, 1u);
    ASSERT_TRUE(warm.error.empty());
    ASSERT_EQ(warm.metadata, root.metadata);
    ASSERT_FALSE(warm.metadata.empty());
    ASSERT_EQ(warm.files[0].hunks[0].lines, root.files[0].hunks[0].lines);
    { std::ofstream file(path / "app.cpp"); file << "after\n"; }
    ASSERT_TRUE(git::git_run(directory, commit).success());
    auto next = git::load_commit_patch_async({directory, "HEAD", root.resolvedCommit, 0}).get();
    ASSERT_TRUE(next.error.empty());
    ASSERT_EQ(next.resolvedParent, root.resolvedCommit);
    ASSERT_EQ(next.files[0].hunks[0].lines.back(), "+after");
    ASSERT_NE(next.resolvedCommit, root.resolvedCommit);
    ASSERT_FALSE(git::read_commit_patch({directory, "HEAD", "missing-parent"}).error.empty());
    ASSERT_FALSE(git::read_commit_patch({directory, std::string(40, 'f')}).error.empty());
    ASSERT_FALSE(git::read_commit_patch({directory, std::string(40, 'f'), root.resolvedCommit}).error.empty());
    std::stop_source cancelled;
    cancelled.request_stop();
    ASSERT_TRUE(git::read_commit_patch({directory, "HEAD"}, cancelled.get_token()).error.find("cancelled") != std::string::npos);
    std::filesystem::remove_all(path);
}

TEST(merge_parent_cache_entries_are_distinct_and_missing_cached_objects_fail) {
    char pattern[] = "/tmp/fh-commit-merge.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    std::filesystem::path path(directory);
    auto run = [&](std::vector<std::string> args) {
        args.insert(args.begin(), {"-c", "user.name=Test", "-c", "user.email=test@example.invalid"});
        return git::git_run(directory, args);
    };
    ASSERT_TRUE(run({"init", "-q"}).success());
    { std::ofstream file(path / "root"); file << "root\n"; }
    ASSERT_TRUE(run({"add", "."}).success());
    ASSERT_TRUE(run({"commit", "-qm", "root"}).success());
    ASSERT_TRUE(run({"branch", "side"}).success());
    { std::ofstream file(path / "main"); file << "main\n"; }
    ASSERT_TRUE(run({"add", "."}).success());
    ASSERT_TRUE(run({"commit", "-qm", "main"}).success());
    ASSERT_TRUE(run({"checkout", "side"}).success());
    { std::ofstream file(path / "side"); file << "side\n"; }
    ASSERT_TRUE(run({"add", "."}).success());
    ASSERT_TRUE(run({"commit", "-qm", "side"}).success());
    ASSERT_TRUE(run({"checkout", "-"}).success());
    ASSERT_TRUE(run({"merge", "--no-ff", "side", "-m", "merge"}).success());
    auto primary = git::read_commit_patch({directory, "HEAD"});
    auto secondary = git::read_commit_patch({directory, "HEAD", "HEAD^2"});
    ASSERT_TRUE(primary.error.empty());
    ASSERT_TRUE(secondary.error.empty());
    ASSERT_NE(primary.resolvedParent, secondary.resolvedParent);
    ASSERT_EQ(primary.files[0].filePath, "side");
    ASSERT_EQ(secondary.files[0].filePath, "main");
    for (const auto& parent : {std::string{}, primary.resolvedParent, secondary.resolvedParent}) {
        ASSERT_TRUE(git::read_commit_patch({directory, primary.resolvedCommit, parent}).error.empty());
        size_t commands = 0;
        git::set_log_callback([&](const auto&, const auto&, const auto&, bool) { ++commands; });
        auto warm = git::read_commit_patch({directory, primary.resolvedCommit, parent});
        git::set_log_callback({});
        ASSERT_EQ(commands, 1u);
        ASSERT_TRUE(warm.error.empty());
        ASSERT_EQ(warm.resolvedParent, parent.empty() ? primary.resolvedParent : parent);
    }
    auto object = path / ".git/objects" / primary.resolvedCommit.substr(0, 2) / primary.resolvedCommit.substr(2);
    ASSERT_TRUE(std::filesystem::remove(object));
    ASSERT_FALSE(git::read_commit_patch({directory, primary.resolvedCommit}).error.empty());
    std::filesystem::remove_all(path);
}

TEST(commit_patch_cache_invalidates_when_a_shallow_boundary_is_deepened) {
    char pattern[] = "/tmp/fh-commit-shallow.XXXXXX";
    auto* directory = mkdtemp(pattern);
    ASSERT_TRUE(directory != nullptr);
    std::filesystem::path path(directory);
    ASSERT_TRUE(git::git_run(directory, {"init", "-q"}).success());
    const std::vector<std::string> commit{"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "commit", "-qam", "fixture"};
    { std::ofstream file(path / "app.cpp"); file << "before\n"; }
    ASSERT_TRUE(git::git_run(directory, {"add", "."}).success());
    ASSERT_TRUE(git::git_run(directory, commit).success());
    { std::ofstream file(path / "app.cpp"); file << "after\n"; }
    ASSERT_TRUE(git::git_run(directory, commit).success());
    auto shallow = path / "shallow";
    ASSERT_TRUE(git::git_run(directory, {"clone", "--depth=1", "file://" + path.string(), shallow.string()}).success());
    auto boundary = git::read_commit_patch({shallow.string(), "HEAD"});
    ASSERT_TRUE(boundary.error.empty());
    ASSERT_TRUE(boundary.resolvedParent.empty());
    ASSERT_TRUE(boundary.files[0].isNew);
    ASSERT_TRUE(git::git_run(shallow.string(), {"fetch", "--deepen=1"}).success());
    auto complete = git::read_commit_patch({shallow.string(), boundary.resolvedCommit});
    ASSERT_TRUE(complete.error.empty());
    ASSERT_FALSE(complete.resolvedParent.empty());
    ASSERT_FALSE(complete.files[0].isNew);
    ASSERT_EQ(complete.files[0].hunks[0].lines.front(), "-before");
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

TEST(commit_patch_cache_bounds_all_owned_content_and_evicts_least_recently_used) {
    git::CommitPatchKey a{"repo", "common", "aaaaaaaa", "parent", 3, false};
    auto b = a; b.commit = "bbbbbbbb";
    auto c = a; c.commit = "cccccccc";
    ecs::CommitPatch patch;
    patch.metadata = std::string(2000, 'm');
    patch.resolvedCommit = a.commit;
    patch.resolvedParent = a.parent;
    ecs::FileDiff file;
    file.filePath = "app.cpp";
    file.oldMode = "100644";
    file.newMode = "100755";
    ecs::DiffHunk hunk;
    hunk.header = "@@ -1 +1 @@";
    hunk.lines = {"-before", "+after"};
    hunk.noNewline.insert(1);
    hunk.movedLines.insert(0);
    file.hunks.push_back(hunk);
    patch.files.push_back(file);
    auto cost = git::commit_patch_owned_bytes(a, patch);
    git::CommitPatchCache cache(cost * 2);
    ASSERT_TRUE(cache.put(a, patch));
    ASSERT_TRUE(cache.put(b, patch));
    ASSERT_TRUE(cache.bytes() <= cost * 2);
    auto owned = cache.get(a);
    ASSERT_TRUE(owned.has_value());
    ASSERT_EQ(owned->files[0].renderIdentity, patch.files[0].renderIdentity);
    owned->metadata = "changed";
    ASSERT_EQ(cache.get(a)->metadata, patch.metadata);
    owned->files[0].hunks[0].lines[0] = "edited in view";
    ASSERT_EQ(cache.get(a)->files[0].hunks[0].lines[0], "-before");
    ASSERT_TRUE(cache.put(c, patch));
    ASSERT_FALSE(cache.get(b).has_value());
    ASSERT_TRUE(cache.get(a).has_value());
    ASSERT_TRUE(cache.bytes() <= cost * 2);
    auto huge = patch;
    huge.files[0].hunks[0].lines.push_back(std::string(cost * 3, 'x'));
    ASSERT_FALSE(cache.put(b, huge));
    ASSERT_TRUE(cache.bytes() <= cost * 2);
    auto original = git::commit_patch_owned_bytes(a, patch);
    patch.files[0].hunks[0].lines[0].reserve(4096);
    ASSERT_TRUE(git::commit_patch_owned_bytes(a, patch) > original + 4000);
    original = git::commit_patch_owned_bytes(a, patch);
    patch.files[0].oldObject.reserve(4096);
    patch.files[0].newObject.reserve(4096);
    ASSERT_TRUE(git::commit_patch_owned_bytes(a, patch) > original + 8000);
}

TEST(commit_patch_cache_keys_include_repository_parent_and_all_diff_options) {
    git::CommitPatchKey key{"repo", "common", "commit", "parent", 3, false};
    git::CommitPatchCache cache;
    ASSERT_TRUE(cache.put(key, {}));
    for (int i = 0; i < 7; ++i) {
        auto changed = key;
        if (i == 0) changed.repository = "another repo";
        if (i == 1) changed.commonDirectory = "another common directory";
        if (i == 2) changed.commit = "another commit";
        if (i == 3) changed.parent = "another parent";
        if (i == 4) changed.context = 23;
        if (i == 5) changed.ignoreWhitespace = true;
        if (i == 6) changed.firstParent = true;
        ASSERT_FALSE(cache.get(changed).has_value());
    }
    ecs::CommitPatch failed;
    failed.error = "failed";
    ASSERT_FALSE(cache.put(key, failed));
}

int main() { RUN_ALL_TESTS(); }

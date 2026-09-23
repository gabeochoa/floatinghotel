#include "test_framework.h"
#include "../../src/git/git_commands.h"
#include "../../src/git/git_parser.h"
#include "../../src/git/git_runner.h"

#include <filesystem>
#include <fstream>
#include <unistd.h>

static std::string run_out(const std::filesystem::path& repo, std::vector<std::string> args) {
    auto result = git::git_run(repo.string(), args);
    return result.stdout_str();
}

// Approve == git add --patch: whole hunk stages, selected lines split a hunk.
TEST(approve_stages_one_hunk_and_splits_another_by_selected_lines) {
    char directory[] = "/tmp/fh-stage-approve.XXXXXX";
    auto* made = mkdtemp(directory);
    ASSERT_TRUE(made != nullptr);
    std::filesystem::path repo = made;
    auto git = [&](std::vector<std::string> args) { return git::git_run(repo.string(), args); };
    ASSERT_TRUE(git({"init", "-q", "-b", "main"}).success());
    ASSERT_TRUE(git({"config", "user.name", "Stage fixture"}).success());
    ASSERT_TRUE(git({"config", "user.email", "stage@example.invalid"}).success());
    {
        std::ofstream out(repo / "a.cpp");
        for (int i = 1; i <= 60; ++i) out << "line " << i << "\n";
    }
    ASSERT_TRUE(git({"add", "."}).success());
    ASSERT_TRUE(git({"commit", "-qm", "base"}).success());
    {
        std::ofstream out(repo / "a.cpp");
        for (int i = 1; i <= 60; ++i) {
            if (i == 10) out << "ten changed\n";
            else if (i == 30) out << "thirty changed\n";
            else if (i == 50) out << "fifty changed\nextra fifty\n";
            else out << "line " << i << "\n";
        }
    }
    auto files = git::parse_diff(run_out(repo, {"diff", "--unified=3"}));
    ASSERT_EQ(files.size(), 1u);
    ASSERT_EQ(files[0].hunks.size(), 3u);

    ASSERT_TRUE(git::stage_hunk(repo.string(), files[0], files[0].hunks[0]).success());
    std::vector<std::set<size_t>> selected(files[0].hunks.size());
    // Hunk at line 50: keep only the "+fifty changed" replacement line.
    for (size_t i = 0; i < files[0].hunks[2].lines.size(); ++i)
        if (files[0].hunks[2].lines[i] == "+fifty changed") selected[2].insert(i);
    ASSERT_TRUE(git::stage_selected_lines(repo.string(), files[0], selected).success());

    auto staged = run_out(repo, {"diff", "--cached", "--unified=3"});
    auto unstaged = run_out(repo, {"diff", "--unified=3"});
    bool ok = staged.contains("ten changed") && staged.contains("fifty changed") &&
              !staged.contains("extra fifty") && !staged.contains("thirty changed") &&
              unstaged.contains("thirty changed") && unstaged.contains("extra fifty") &&
              !unstaged.contains("ten changed");
    std::filesystem::remove_all(repo);
    ASSERT_TRUE(ok);
}

int main() { RUN_ALL_TESTS(); }

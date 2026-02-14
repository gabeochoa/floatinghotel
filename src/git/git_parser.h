#pragma once

#include <string>
#include <vector>

#include "../ecs/components.h"  // FileStatus, CommitEntry, FileDiff, DiffHunk

namespace git {

// ---- Status Parser (T012) ----

struct StatusResult {
    std::string branchName;
    std::string upstreamBranch;
    int aheadCount = 0;
    int behindCount = 0;
    bool isDetachedHead = false;
    std::vector<ecs::FileStatus> stagedFiles;
    std::vector<ecs::FileStatus> unstagedFiles;
    std::vector<std::string> untrackedFiles;
};

// Parse output of: git status --porcelain=v2 --branch
StatusResult parse_status(const std::string& porcelain_output);

// ---- Log Parser (T013) ----

// Parse output of: git log --format="%H%x00%h%x00%s%x00%an%x00%aI%x00%D"
// Fields are NUL-separated: hash, shortHash, subject, author, date, decorations
std::vector<ecs::CommitEntry> parse_log(const std::string& log_output);

// ---- Diff Parser (T014) ----

// Parse unified diff output from: git diff / git diff --staged / git show
std::vector<ecs::FileDiff> parse_diff(const std::string& diff_output);

// ---- Branch Parser (T031) ----

// Parse output of: git branch --list --format="%(refname:short)|%(objectname:short)|%(HEAD)|%(upstream:short)|%(upstream:track)"
std::vector<ecs::BranchInfo> parse_branch_list(const std::string& branch_output);

}  // namespace git

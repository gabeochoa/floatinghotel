#pragma once

#include <string>
#include <vector>

#include "../ecs/components.h"
#include "git_runner.h"

namespace git {

// Build a unified diff patch string for a single hunk (exposed for testing)
std::string build_patch(const ecs::FileDiff& file_diff,
                        const ecs::DiffHunk& hunk);

// Stage a single hunk by writing it as a patch and applying
GitResult stage_hunk(const std::string& repo_path,
                     const ecs::FileDiff& file_diff,
                     const ecs::DiffHunk& hunk);

// Unstage a single hunk (reverse apply to index)
GitResult unstage_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk);

// Discard a single hunk from working tree (destructive!)
GitResult discard_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk);

// Stage a single file
GitResult stage_file(const std::string& repo_path,
                     const std::string& file_path);

// Unstage a single file
GitResult unstage_file(const std::string& repo_path,
                       const std::string& file_path);

// Stage all files
GitResult stage_all(const std::string& repo_path);

// Unstage all files
GitResult unstage_all(const std::string& repo_path);

// Create a new branch from a starting point (defaults to HEAD)
GitResult create_branch(const std::string& repo_path,
                        const std::string& name,
                        const std::string& from = "HEAD");

// Delete a branch (force=true uses -D for unmerged branches)
GitResult delete_branch(const std::string& repo_path,
                        const std::string& name, bool force = false);

// Checkout/switch to an existing branch
GitResult checkout_branch(const std::string& repo_path,
                          const std::string& name);

}  // namespace git

#include "error_humanizer.h"

#include <vector>

namespace git {

struct ErrorPattern {
    std::string pattern;   // Substring to search for in stderr
    std::string friendly;  // User-friendly replacement message
};

static const std::vector<ErrorPattern> ERROR_PATTERNS = {
    {"Authentication failed",
     "Git credentials not configured. Run 'git config credential.helper' to "
     "set up."},
    {"could not read Username",
     "Git credentials not configured. Set up a credential helper."},
    {"CONFLICT",
     "Merge conflict detected. Resolve conflicts before committing."},
    {"non-fast-forward", "Remote has new commits. Pull before pushing."},
    {"rejected", "Push rejected by remote. Pull latest changes first."},
    {"detached HEAD",
     "You are in detached HEAD state. Create a branch to save your work."},
    {"not a git repository", "This directory is not a git repository."},
    {"pathspec", "The specified file or path was not found."},
    {"nothing to commit", "No changes to commit. Stage some changes first."},
    {"already exists", "A branch with that name already exists."},
    {"not fully merged",
     "Branch has unmerged changes. Use force delete (-D) to delete anyway."},
    {"Your local changes",
     "You have uncommitted changes. Commit or stash them first."},
};

std::string humanize_error(const std::string& stderr_str) {
    for (const auto& ep : ERROR_PATTERNS) {
        if (stderr_str.find(ep.pattern) != std::string::npos) {
            return ep.friendly;
        }
    }
    return stderr_str;  // Fallback: return raw stderr
}

}  // namespace git

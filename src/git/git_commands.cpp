#include "git_commands.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace git {

// Build a minimal unified diff patch string for a single hunk.
std::string build_patch(const ecs::FileDiff& file_diff,
                               const ecs::DiffHunk& hunk) {
    std::string patch;

    // --- a/ and +++ b/ lines
    std::string old_path = file_diff.oldPath.empty()
                               ? file_diff.filePath
                               : file_diff.oldPath;
    if (file_diff.isNew) {
        patch += "--- /dev/null\n";
    } else {
        patch += "--- a/" + old_path + "\n";
    }
    if (file_diff.isDeleted) {
        patch += "+++ /dev/null\n";
    } else {
        patch += "+++ b/" + file_diff.filePath + "\n";
    }

    // Hunk header (@@ ... @@)
    patch += hunk.header + "\n";

    // Hunk content lines (already have +/-/space prefix)
    for (const auto& line : hunk.lines) {
        patch += line + "\n";
    }

    return patch;
}

// Write patch to a temp file and return its path.
static std::string write_temp_patch(const std::string& patch_content) {
    // Use mkstemp for safe temp file creation
    std::string tmpl = std::filesystem::temp_directory_path().string()
                       + "/fh_patch_XXXXXX";
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');

    int fd = mkstemp(buf.data());
    if (fd < 0) return "";

    std::string path(buf.data());
    // Write content via fd
    auto written = write(fd, patch_content.data(), patch_content.size());
    (void)written;
    close(fd);
    return path;
}

GitResult stage_hunk(const std::string& repo_path,
                     const ecs::FileDiff& file_diff,
                     const ecs::DiffHunk& hunk) {
    std::string patch = build_patch(file_diff, hunk);
    std::string tmp_path = write_temp_patch(patch);
    if (tmp_path.empty()) {
        return GitResult{{.stdout_str = "", .stderr_str = "Failed to create temp patch file", .exit_code = -1}};
    }
    auto result = git_run(repo_path, {"apply", "--cached", tmp_path});
    std::filesystem::remove(tmp_path);
    return result;
}

GitResult unstage_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk) {
    std::string patch = build_patch(file_diff, hunk);
    std::string tmp_path = write_temp_patch(patch);
    if (tmp_path.empty()) {
        return GitResult{{.stdout_str = "", .stderr_str = "Failed to create temp patch file", .exit_code = -1}};
    }
    auto result = git_run(repo_path, {"apply", "--cached", "--reverse", tmp_path});
    std::filesystem::remove(tmp_path);
    return result;
}

GitResult discard_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk) {
    std::string patch = build_patch(file_diff, hunk);
    std::string tmp_path = write_temp_patch(patch);
    if (tmp_path.empty()) {
        return GitResult{{.stdout_str = "", .stderr_str = "Failed to create temp patch file", .exit_code = -1}};
    }
    auto result = git_run(repo_path, {"apply", "--reverse", tmp_path});
    std::filesystem::remove(tmp_path);
    return result;
}

GitResult stage_file(const std::string& repo_path,
                     const std::string& file_path) {
    return git_run(repo_path, {"add", "--", file_path});
}

GitResult unstage_file(const std::string& repo_path,
                       const std::string& file_path) {
    return git_run(repo_path, {"restore", "--staged", "--", file_path});
}

GitResult stage_all(const std::string& repo_path) {
    return git_run(repo_path, {"add", "-A"});
}

GitResult unstage_all(const std::string& repo_path) {
    return git_run(repo_path, {"restore", "--staged", "."});
}

GitResult create_branch(const std::string& repo_path,
                        const std::string& name,
                        const std::string& from) {
    return git_run(repo_path, {"switch", "-c", name, from});
}

GitResult delete_branch(const std::string& repo_path,
                        const std::string& name, bool force) {
    return git_run(repo_path, {"branch", force ? "-D" : "-d", name});
}

GitResult checkout_branch(const std::string& repo_path,
                          const std::string& name) {
    return git_run(repo_path, {"switch", name});
}

}  // namespace git

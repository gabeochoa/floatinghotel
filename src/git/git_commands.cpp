#include "git_commands.h"

#include <filesystem>
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

// Write the hunk to a temp patch and `git apply` it with the given flags,
// cleaning up the temp file. Shared by stage/unstage/discard (they differ only
// in the apply flags).
static GitResult apply_hunk_patch(const std::string& repo_path,
                                  const ecs::FileDiff& file_diff,
                                  const ecs::DiffHunk& hunk,
                                  std::vector<std::string> flags) {
    std::string tmp_path = write_temp_patch(build_patch(file_diff, hunk));
    if (tmp_path.empty()) {
        return GitResult{{.stdout_str = "", .stderr_str = "Failed to create temp patch file", .exit_code = -1}};
    }
    std::vector<std::string> args = {"apply"};
    args.insert(args.end(), flags.begin(), flags.end());
    args.push_back(tmp_path);
    auto result = git_run(repo_path, args);
    std::filesystem::remove(tmp_path);
    return result;
}

GitResult stage_hunk(const std::string& repo_path,
                     const ecs::FileDiff& file_diff,
                     const ecs::DiffHunk& hunk) {
    return apply_hunk_patch(repo_path, file_diff, hunk, {"--cached"});
}

GitResult unstage_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk) {
    return apply_hunk_patch(repo_path, file_diff, hunk, {"--cached", "--reverse"});
}

GitResult discard_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk) {
    return apply_hunk_patch(repo_path, file_diff, hunk, {"--reverse"});
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

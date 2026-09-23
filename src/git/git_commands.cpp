#include "git_commands.h"

#include <filesystem>
#include <unistd.h>

namespace git {

std::optional<ecs::DiffHunk> selected_lines_hunk(const ecs::DiffHunk& hunk,
                                                const std::set<size_t>& selected) {
    ecs::DiffHunk out = hunk;
    out.lines.clear();
    out.noNewline.clear();
    out.oldCount = out.newCount = 0;
    bool changed = false;
    for (size_t i = 0; i < hunk.lines.size(); ++i) {
        std::string line = hunk.lines[i];
        char sign = line.empty() ? ' ' : line.front();
        if (sign == '+' && !selected.contains(i)) continue;
        if (sign == '-' && !selected.contains(i)) line[0] = ' ';
        if (selected.contains(i) && (sign == '+' || sign == '-')) changed = true;
        if (line.empty() || line[0] != '+') ++out.oldCount;
        if (line.empty() || line[0] != '-') ++out.newCount;
        if (hunk.noNewline.contains(i)) out.noNewline.insert(out.lines.size());
        out.lines.push_back(std::move(line));
    }
    if (!changed) return std::nullopt;
    if (out.newCount > 0 && out.newStart == 0) out.newStart = std::max(1, out.oldStart);
    out.header = "@@ -" + std::to_string(out.oldStart) + "," + std::to_string(out.oldCount) +
                 " +" + std::to_string(out.newStart) + "," + std::to_string(out.newCount) + " @@";
    return out;
}

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
        if (hunk.noNewline.contains(static_cast<size_t>(&line - hunk.lines.data())))
            patch += "\\ No newline at end of file\n";
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
static GitResult apply_patch_text(const std::string& repo_path,
                                  const std::string& patch,
                                  std::vector<std::string> flags) {
    std::string tmp_path = write_temp_patch(patch);
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
    return apply_patch_text(repo_path, build_patch(file_diff, hunk), {"--cached"});
}

GitResult unstage_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk) {
    return apply_patch_text(repo_path, build_patch(file_diff, hunk), {"--cached", "--reverse"});
}

GitResult discard_hunk(const std::string& repo_path,
                       const ecs::FileDiff& file_diff,
                       const ecs::DiffHunk& hunk) {
    return apply_patch_text(repo_path, build_patch(file_diff, hunk), {"--reverse"});
}

static GitResult apply_selected_lines(const std::string& repo_path, const ecs::FileDiff& file,
                                      const std::vector<std::set<size_t>>& selected,
                                      const std::vector<std::string>& flags);

GitResult stage_selected_lines(const std::string& repo_path, const ecs::FileDiff& file,
                                const std::vector<std::set<size_t>>& selected) {
    return apply_selected_lines(repo_path, file, selected, {"--cached"});
}

GitResult unstage_selected_lines(const std::string& repo_path, const ecs::FileDiff& file,
                                  const std::vector<std::set<size_t>>& selected) {
    return apply_selected_lines(repo_path, file, selected, {"--cached", "--reverse"});
}

static GitResult apply_selected_lines_impl(const std::string& repo_path, const ecs::FileDiff& file,
                                const std::vector<std::set<size_t>>& selected,
                                const std::vector<std::string>& flags) {
    if (selected.size() != file.hunks.size() || file.isBinary || file.isSubmodule || file.isRenamed)
        return GitResult{{.stderr_str = "This selection cannot be staged as lines", .exit_code = -1}};
    std::vector<ecs::DiffHunk> hunks;
    auto partialFile = file;
    for (size_t i = 0; i < file.hunks.size(); ++i) {
        if (!selected[i].empty() && *selected[i].rbegin() >= file.hunks[i].lines.size())
            return GitResult{{.stderr_str = "Selection is outside the diff", .exit_code = -1}};
        for (size_t line = 0; line < file.hunks[i].lines.size(); ++line)
            if (file.hunks[i].lines[line].starts_with('-') && !selected[i].contains(line))
                partialFile.isDeleted = false;
        auto hunk = selected_lines_hunk(file.hunks[i], selected[i]);
        if (hunk) {
            if (hunk->newCount > 0) partialFile.isDeleted = false;
            hunks.push_back(std::move(*hunk));
        }
    }
    std::string patch;
    for (const auto& hunk : hunks) {
        std::string part = build_patch(partialFile, hunk);
        patch += patch.empty() ? part : part.substr(part.find("@@"));
    }
    if (patch.empty()) return GitResult{{.stderr_str = "Select added or removed lines first", .exit_code = -1}};
    return apply_patch_text(repo_path, patch, flags);
}

static GitResult apply_selected_lines(const std::string& repo_path, const ecs::FileDiff& file,
                                      const std::vector<std::set<size_t>>& selected,
                                      const std::vector<std::string>& flags) {
    return apply_selected_lines_impl(repo_path, file, selected, flags);
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

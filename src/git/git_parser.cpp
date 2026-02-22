#include "git_parser.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <string_view>

namespace git {

namespace {

// Find the Nth space in a string, return position after it.
// Returns std::string::npos if not enough spaces found.
size_t skip_fields(const std::string& line, int count) {
    size_t pos = 0;
    for (int i = 0; i < count; ++i) {
        pos = line.find(' ', pos);
        if (pos == std::string::npos) return pos;
        ++pos;
    }
    return pos;
}

}  // namespace

// ---- Status Parser (T012) ----

StatusResult parse_status(const std::string& output) {
    StatusResult result;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        if (line.starts_with("# branch.head ")) {
            result.branchName = line.substr(14);
            if (result.branchName == "(detached)") {
                result.isDetachedHead = true;
            }
        } else if (line.starts_with("# branch.upstream ")) {
            result.upstreamBranch = line.substr(18);
        } else if (line.starts_with("# branch.ab ")) {
            // Format: # branch.ab +<ahead> -<behind>
            std::sscanf(line.c_str(), "# branch.ab +%d -%d",
                        &result.aheadCount, &result.behindCount);
        } else if (line.starts_with("1 ") || line.starts_with("2 ")) {
            // Ordinary (1) or rename/copy (2) entry
            // Format for type 1:
            //   1 XY sub mH mI mW hH hI <path>
            //   Fields are space-separated, path is everything after 8th space
            //
            // Format for type 2:
            //   2 XY sub mH mI mW hH hI X<score> <path>\t<origPath>
            //   Fields are space-separated, path\torigPath after 9th space

            if (line.size() < 4) continue;

            ecs::FileStatus fs;
            fs.indexStatus = line[2];
            fs.workTreeStatus = line[3];

            if (line[0] == '1') {
                // Ordinary changed entry: path starts after the 8th space
                size_t path_start = skip_fields(line, 8);
                if (path_start != std::string::npos) {
                    fs.path = line.substr(path_start);
                }
            } else {
                // Rename/copy entry: path\torigPath after the 9th space
                size_t path_start = skip_fields(line, 9);
                if (path_start != std::string::npos) {
                    std::string paths = line.substr(path_start);
                    size_t tab_pos = paths.find('\t');
                    if (tab_pos != std::string::npos) {
                        fs.path = paths.substr(0, tab_pos);
                        fs.origPath = paths.substr(tab_pos + 1);
                    } else {
                        fs.path = paths;
                    }
                }
            }

            // Classify into staged vs unstaged based on index/worktree status
            // '.' means no change in that column
            if (fs.indexStatus != '.') {
                result.stagedFiles.push_back(fs);
            }
            if (fs.workTreeStatus != '.') {
                // For unstaged entries, create a copy so staged/unstaged
                // lists are independent
                ecs::FileStatus unstaged_fs = fs;
                result.unstagedFiles.push_back(unstaged_fs);
            }
        } else if (line.starts_with("u ")) {
            // Unmerged entry
            // Format: u XY sub m1 m2 m3 mW h1 h2 h3 <path>
            if (line.size() < 4) continue;

            ecs::FileStatus fs;
            fs.indexStatus = line[2];
            fs.workTreeStatus = line[3];

            size_t path_start = skip_fields(line, 10);
            if (path_start != std::string::npos) {
                fs.path = line.substr(path_start);
            }

            // Unmerged files appear in both lists
            result.stagedFiles.push_back(fs);
            result.unstagedFiles.push_back(fs);
        } else if (line.starts_with("? ")) {
            // Untracked file
            result.untrackedFiles.push_back(line.substr(2));
        } else if (line.starts_with("! ")) {
            // Ignored file -- skip
        }
    }

    return result;
}

// ---- Log Parser (T013) ----

std::vector<ecs::CommitEntry> parse_log(const std::string& log_output) {
    std::vector<ecs::CommitEntry> entries;
    std::istringstream stream(log_output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // Format: hash\0shortHash\0subject\0author\0date\0decorations
        // Fields separated by NUL character (\0, from %x00 in git format)
        ecs::CommitEntry entry;
        std::vector<std::string> fields;
        size_t prev = 0;
        size_t pos = 0;
        while ((pos = line.find('\0', prev)) != std::string::npos) {
            fields.push_back(line.substr(prev, pos - prev));
            prev = pos + 1;
        }
        fields.push_back(line.substr(prev));  // last field

        if (fields.size() >= 5) {
            entry.hash = fields[0];
            entry.shortHash = fields[1];
            entry.subject = fields[2];
            entry.author = fields[3];
            entry.authorDate = fields[4];
            if (fields.size() > 5) entry.decorations = fields[5];
            if (fields.size() > 6) entry.parentHashes = fields[6];
            entries.push_back(std::move(entry));
        }
    }

    return entries;
}

// ---- Diff Parser (T014) ----

std::vector<ecs::FileDiff> parse_diff(const std::string& diff_output) {
    std::vector<ecs::FileDiff> diffs;
    std::istringstream stream(diff_output);
    std::string line;
    ecs::FileDiff* currentFile = nullptr;
    ecs::DiffHunk* currentHunk = nullptr;

    while (std::getline(stream, line)) {
        // Remove trailing \r for Windows-style line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.starts_with("diff --git ")) {
            // New file diff: "diff --git a/path b/path"
            diffs.emplace_back();
            currentFile = &diffs.back();
            currentHunk = nullptr;

            // Parse paths from "diff --git a/path b/path"
            // Find " b/" scanning from the right side to handle paths with
            // spaces. The last " b/" is the separator.
            std::string rest = line.substr(11);  // after "diff --git "
            size_t b_sep = rest.rfind(" b/");
            if (b_sep != std::string::npos) {
                std::string a_path = rest.substr(0, b_sep);
                if (a_path.starts_with("a/")) {
                    a_path = a_path.substr(2);
                }
                currentFile->filePath = a_path;
                currentFile->oldPath = a_path;
            }
        } else if (line.starts_with("--- ")) {
            if (currentFile) {
                std::string path = line.substr(4);
                if (path == "/dev/null") {
                    currentFile->isNew = true;
                } else if (path.starts_with("a/")) {
                    currentFile->oldPath = path.substr(2);
                }
            }
        } else if (line.starts_with("+++ ")) {
            if (currentFile) {
                std::string path = line.substr(4);
                if (path == "/dev/null") {
                    currentFile->isDeleted = true;
                } else if (path.starts_with("b/")) {
                    currentFile->filePath = path.substr(2);
                }
            }
        } else if (line.starts_with("@@ ")) {
            // Hunk header: "@@ -oldStart,oldCount +newStart,newCount @@ context"
            if (currentFile) {
                currentFile->hunks.emplace_back();
                currentHunk = &currentFile->hunks.back();
                currentHunk->header = line;

                // Parse the hunk range numbers
                int oldStart = 0, oldCount = 1, newStart = 0, newCount = 1;
                // Try the full format first: @@ -a,b +c,d @@
                int matched = std::sscanf(line.c_str(),
                                          "@@ -%d,%d +%d,%d @@",
                                          &oldStart, &oldCount,
                                          &newStart, &newCount);
                if (matched < 4) {
                    // Handle variants without comma (single-line hunks)
                    // e.g. "@@ -1 +1 @@" or "@@ -1 +1,3 @@" or "@@ -1,3 +1 @@"
                    oldCount = 1;
                    newCount = 1;
                    matched = std::sscanf(line.c_str(),
                                          "@@ -%d +%d,%d @@",
                                          &oldStart, &newStart, &newCount);
                    if (matched < 2) {
                        matched = std::sscanf(line.c_str(),
                                              "@@ -%d,%d +%d @@",
                                              &oldStart, &oldCount, &newStart);
                        if (matched < 2) {
                            std::sscanf(line.c_str(),
                                        "@@ -%d +%d @@",
                                        &oldStart, &newStart);
                        }
                    }
                }
                currentHunk->oldStart = oldStart;
                currentHunk->oldCount = oldCount;
                currentHunk->newStart = newStart;
                currentHunk->newCount = newCount;
            }
        } else if (currentHunk && !line.empty() &&
                   (line[0] == '+' || line[0] == '-' || line[0] == ' ')) {
            currentHunk->lines.push_back(line);
            if (line[0] == '+') {
                currentFile->additions++;
            } else if (line[0] == '-') {
                currentFile->deletions++;
            }
        } else if (line.starts_with("rename from ")) {
            if (currentFile) {
                currentFile->isRenamed = true;
                currentFile->oldPath = line.substr(12);
            }
        } else if (line.starts_with("rename to ")) {
            if (currentFile) {
                currentFile->filePath = line.substr(10);
            }
        } else if (line.starts_with("Binary files ")) {
            if (currentFile) {
                currentFile->isBinary = true;
            }
        }
        // "\ No newline at end of file" and other unrecognized lines are
        // silently skipped.
    }

    return diffs;
}

// ---- Branch Parser (T031) ----

std::vector<ecs::BranchInfo> parse_branch_list(const std::string& output) {
    std::vector<ecs::BranchInfo> branches;
    std::istringstream stream(output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;

        // Format: refname|objectname|HEAD|upstream|upstream_track
        // e.g. "main|abc1234|*|origin/main|[ahead 1]"
        // or   "feature|def5678| |origin/feature|"
        ecs::BranchInfo info;
        std::vector<std::string> fields;
        size_t prev = 0;
        size_t pos = 0;
        while ((pos = line.find('|', prev)) != std::string::npos) {
            fields.push_back(line.substr(prev, pos - prev));
            prev = pos + 1;
        }
        fields.push_back(line.substr(prev));

        if (fields.size() < 3) continue;

        info.name = fields[0];
        info.shortHash = fields[1];
        info.isCurrent = (fields[2] == "*");
        info.isLocal = true;

        if (fields.size() > 3) {
            info.upstream = fields[3];
        }
        if (fields.size() > 4) {
            info.tracking = fields[4];
        }

        // Skip detached HEAD entries
        if (info.name == "(HEAD detached" || info.name.find("(HEAD") == 0) {
            continue;
        }

        branches.push_back(std::move(info));
    }

    // Sort: current branch first, then alphabetical
    std::sort(branches.begin(), branches.end(),
              [](const ecs::BranchInfo& a, const ecs::BranchInfo& b) {
                  if (a.isCurrent != b.isCurrent) return a.isCurrent;
                  return a.name < b.name;
              });

    return branches;
}

}  // namespace git

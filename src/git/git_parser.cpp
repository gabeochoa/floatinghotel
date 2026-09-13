#include "git_parser.h"
#include "../util/moved_code.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <sstream>

namespace git {

ecs::BlameLine parse_blame_line(const std::string& output) {
    ecs::BlameLine result;
    std::istringstream stream(output);
    std::string line;
    if (!std::getline(stream, line)) return result;
    std::istringstream header(line);
    if (!(header >> result.hash >> result.originalLine >> result.finalLine) ||
        result.hash.size() != 40 || result.originalLine < 1 || result.finalLine < 1 ||
        result.hash.find_first_not_of("0123456789abcdef") != std::string::npos) return {};
    while (std::getline(stream, line)) {
        if (line.starts_with("author ")) result.author = line.substr(7);
        else if (line.starts_with("summary ")) result.summary = line.substr(8);
        else if (line.starts_with("filename ")) result.file = line.substr(9);
        else if (line.starts_with('\t')) { result.content = line.substr(1); break; }
    }
    return result;
}

std::vector<ecs::SearchMatch> parse_grep_matches(const std::string& output) {
    std::vector<ecs::SearchMatch> matches;
    size_t start = 0;
    while (start < output.size() && matches.size() < 5000) {
        size_t pathEnd = output.find('\0', start);
        if (pathEnd == std::string::npos) break;
        size_t lineEnd = output.find('\0', pathEnd + 1);
        if (lineEnd == std::string::npos) break;
        size_t textEnd = output.find('\n', lineEnd + 1);
        if (textEnd == std::string::npos) textEnd = output.size();
        int line = 0;
        auto parsed = std::from_chars(output.data() + pathEnd + 1, output.data() + lineEnd, line);
        if (parsed.ec == std::errc{} && parsed.ptr == output.data() + lineEnd && line > 0)
            matches.push_back({output.substr(start, pathEnd - start), line,
                               output.substr(lineEnd + 1, textEnd - lineEnd - 1)});
        start = textEnd + 1;
    }
    return matches;
}

std::vector<std::string> parse_null_paths(const std::string& output) {
    std::vector<std::string> paths;
    size_t start = 0;
    while (start < output.size()) {
        size_t end = output.find('\0', start);
        if (end == std::string::npos) end = output.size();
        if (end > start) paths.push_back(output.substr(start, end - start));
        start = end + 1;
    }
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}

namespace {

std::pair<std::string, size_t> decode_path(std::string_view input) {
    if (!input.starts_with('"')) return {std::string(input), input.size()};
    std::string path;
    size_t i = 1;
    while (i < input.size()) {
        char c = input[i++];
        if (c == '"') break;
        if (c != '\\' || i == input.size()) { path += c; continue; }
        c = input[i++];
        if (c >= '0' && c <= '7') {
            unsigned value = static_cast<unsigned>(c - '0');
            for (int digits = 1; digits < 3 && i < input.size() && input[i] >= '0' && input[i] <= '7'; ++digits)
                value = value * 8 + static_cast<unsigned>(input[i++] - '0');
            path += static_cast<char>(value);
        } else {
            switch (c) {
                case 'a': path += '\a'; break;
                case 'b': path += '\b'; break;
                case 't': path += '\t'; break;
                case 'n': path += '\n'; break;
                case 'v': path += '\v'; break;
                case 'f': path += '\f'; break;
                case 'r': path += '\r'; break;
                case '"': path += '"'; break;
                case '\\': path += '\\'; break;
                default: path += '\\'; path += c; break;
            }
        }
    }
    return {std::move(path), i};
}

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

// Split on a single-char delimiter, keeping empty fields (incl. a trailing one).
std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    size_t prev = 0, pos;
    while ((pos = s.find(delim, prev)) != std::string::npos) {
        out.push_back(s.substr(prev, pos - prev));
        prev = pos + 1;
    }
    out.push_back(s.substr(prev));
    return out;
}

}  // namespace

// ---- Status Parser (T012) ----

StatusResult parse_status(const std::string& output) {
    StatusResult result;
    std::istringstream stream(output);
    std::string line;

    const bool nulTerminated = output.find('\0') != std::string::npos;
    while (std::getline(stream, line, nulTerminated ? '\0' : '\n')) {
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
            // porcelain v2 'sub' field (index 5): "S..." for a submodule gitlink.
            fs.isSubmodule = line.size() > 5 && line[5] == 'S';

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
                    if (nulTerminated) {
                        fs.path = paths;
                        std::getline(stream, fs.origPath, '\0');
                    } else if (tab_pos != std::string::npos) {
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
                // push_back copies, so staged/unstaged lists stay independent.
                result.unstagedFiles.push_back(fs);
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
        std::vector<std::string> fields = split(line, '\0');

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
    bool transportCRLF = false;

    while (std::getline(stream, line)) {
        if (line.starts_with("diff --git ")) transportCRLF = line.ends_with('\r');
        bool contentLine = currentHunk && !line.empty() &&
                           (line.front() == '+' || line.front() == '-' || line.front() == ' ');
        if (!line.empty() && line.back() == '\r' && (transportCRLF || !contentLine)) {
            line.pop_back();
        }

        if (line.starts_with("diff --git ")) {
            // New file diff: "diff --git a/path b/path"
            diffs.emplace_back();
            currentFile = &diffs.back();
            currentHunk = nullptr;

            std::string_view rest(line.data() + 11, line.size() - 11);
            size_t separator = std::string_view::npos;
            if (rest.starts_with('"')) separator = decode_path(rest).second;
            else {
                separator = rest.find(" \"b/");
                if (separator == std::string_view::npos) separator = rest.rfind(" b/");
            }
            if (separator != std::string_view::npos && separator < rest.size()) {
                auto oldPath = decode_path(rest.substr(0, separator)).first;
                auto newPath = decode_path(rest.substr(separator + 1)).first;
                currentFile->oldPath = oldPath.starts_with("a/") ? oldPath.substr(2) : oldPath;
                currentFile->filePath = newPath.starts_with("b/") ? newPath.substr(2) : newPath;
            }
        } else if (line.starts_with("index ") && currentFile) {
            auto dots = line.find("..", 6);
            if (dots != std::string::npos) {
                currentFile->oldObject = line.substr(6, dots - 6);
                auto end = line.find(' ', dots + 2);
                currentFile->newObject = line.substr(dots + 2, end == std::string::npos ? end : end - dots - 2);
            }
            auto mode = line.rfind(' ');
            if (mode != std::string::npos && line.size() - mode == 7) {
                currentFile->oldMode = currentFile->newMode = line.substr(mode + 1);
            }
            // "index <old>..<new> 160000" marks a submodule (gitlink) change.
            if (line.find(" 160000") != std::string::npos) {
                currentFile->isSubmodule = true;
            }
        } else if (line.starts_with("old mode ") && currentFile) {
            currentFile->oldMode = line.substr(9);
        } else if (line.starts_with("new mode ") && currentFile) {
            currentFile->newMode = line.substr(9);
        } else if (line.starts_with("new file mode ") && currentFile) {
            currentFile->isNew = true;
            currentFile->newMode = line.substr(14);
        } else if (line.starts_with("deleted file mode ") && currentFile) {
            currentFile->isDeleted = true;
            currentFile->oldMode = line.substr(18);
        } else if (line.starts_with("--- ")) {
            if (currentFile) {
                std::string path = decode_path(std::string_view(line).substr(4)).first;
                if (path == "/dev/null") {
                    currentFile->isNew = true;
                } else if (path.starts_with("a/")) {
                    currentFile->oldPath = path.substr(2);
                }
            }
        } else if (line.starts_with("+++ ")) {
            if (currentFile) {
                std::string path = decode_path(std::string_view(line).substr(4)).first;
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
        } else if (line.starts_with("\\ No newline at end of file") && currentHunk && !currentHunk->lines.empty()) {
            currentHunk->noNewline.insert(currentHunk->lines.size() - 1);
        } else if (line.starts_with("rename from ")) {
            if (currentFile) {
                currentFile->isRenamed = true;
                currentFile->oldPath = decode_path(std::string_view(line).substr(12)).first;
            }
        } else if (line.starts_with("rename to ")) {
            if (currentFile) {
                currentFile->filePath = decode_path(std::string_view(line).substr(10)).first;
            }
        } else if (line.starts_with("Binary files ")) {
            if (currentFile) {
                currentFile->isBinary = true;
            }
        }
    }

    moved_code::mark_blocks(diffs);
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
        std::vector<std::string> fields = split(line, '|');

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
        if (info.name.starts_with("(HEAD")) {
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

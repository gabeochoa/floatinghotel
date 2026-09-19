#pragma once

#include "git_runner.h"
#include "../util/fuzzy_match.h"
#include <algorithm>
#include <array>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace git::catalog {

enum class Kind { All, File, Commit, Branch, Tag, Reflog, Stash, Worktree, Submodule, Diagnostic };

inline std::string label(Kind kind) {
    switch (kind) {
        case Kind::All: return "All";
        case Kind::File: return "Files";
        case Kind::Commit: return "Commits";
        case Kind::Branch: return "Branches";
        case Kind::Tag: return "Tags";
        case Kind::Reflog: return "Reflog";
        case Kind::Stash: return "Stashes";
        case Kind::Worktree: return "Worktrees";
        case Kind::Submodule: return "Submodules";
        case Kind::Diagnostic: return "Git diagnostics";
    }
    return {};
}

struct Entry {
    Kind kind = Kind::File;
    std::string identity;
    std::string title;
    std::string detail;
    std::string object;
    bool remote = false;
    std::string parents;
    std::string key() const { return std::to_string(static_cast<int>(kind)) + ":" + identity; }
};

struct Page {
    std::vector<Entry> entries;
    std::string error;
    bool more = false;
};

inline std::vector<std::string> fields(std::string_view text, char delimiter) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < text.size()) {
        const auto end = text.find(delimiter, start);
        out.emplace_back(text.substr(start, end == text.npos ? text.size() - start : end - start));
        if (end == text.npos) break;
        start = end + 1;
    }
    return out;
}

inline std::string trim_line(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

inline bool hash_query(std::string_view query) {
    return query.size() >= 4 && query.size() <= 64 && std::all_of(query.begin(), query.end(), [](unsigned char c) { return std::isxdigit(c); });
}

inline Page refs(const std::string& repository, bool remote, std::stop_token stop) {
    auto result = git_run(repository, {"for-each-ref", "--sort=refname", "--count=10000",
        "--format=%(refname)%00%(objectname)%00%(objecttype)%00%(*objectname)%00%(*objecttype)%00%(subject)",
        remote ? "refs/remotes" : "refs/heads", remote ? "refs/remotes" : "refs/tags"}, stop);
    Page page;
    if (!result.success()) { page.error = result.stderr_str(); return page; }
    for (const auto& line : fields(result.stdout_str(), '\n')) {
        auto f = fields(line, '\0');
        if (f.size() < 5) continue;
        const auto object = f[2] == "commit" ? f[1] : f[4] == "commit" ? f[3] : "";
        if (object.empty()) continue;
        const bool tag = f[0].starts_with("refs/tags/");
        const size_t prefix = tag ? 10 : remote ? 13 : 11;
        page.entries.push_back({tag ? Kind::Tag : Kind::Branch, f[0], f[0].substr(prefix),
            f.size() > 5 ? f[5] : "", object, remote});
    }
    page.more = page.entries.size() >= 10000;
    return page;
}

inline Page commits(const std::string& repository, const std::string& query, std::stop_token stop) {
    Page page;
    if (hash_query(query)) {
        auto resolved = git_run(repository, {"rev-parse", "--verify", "--end-of-options", query + "^{commit}"}, stop);
        if (!resolved.success()) {
            page.error = resolved.stderr_str().find("ambiguous") != std::string::npos ? "Ambiguous commit hash; paste more characters" : "No commit matches this hash";
            return page;
        }
        auto oid = trim_line(resolved.stdout_str());
        auto subject = git_run(repository, {"show", "-s", "--format=%s", oid, "--"}, stop);
        page.entries.push_back({Kind::Commit, oid, trim_line(subject.stdout_str()), oid.substr(0, 12), oid});
        return page;
    }
    std::vector<std::string> args{"log", "--all", "-500", "--fixed-strings", "--regexp-ignore-case", "--format=%H%x00%s%x00%aI"};
    if (!query.empty()) args.push_back("--grep=" + query);
    args.push_back("--");
    auto result = git_run(repository, args, stop);
    if (!result.success()) { page.error = result.stderr_str(); return page; }
    for (const auto& line : fields(result.stdout_str(), '\n')) {
        auto f = fields(line, '\0');
        if (f.size() >= 3) page.entries.push_back({Kind::Commit, f[0], f[1], f[0].substr(0, 12) + " · " + f[2], f[0]});
    }
    page.more = page.entries.size() == 500;
    return page;
}

inline std::vector<Entry> filter(const std::vector<Entry>& entries, Kind kind, const std::string& query) {
    struct Scored { Entry entry; int score; size_t order; };
    std::vector<Scored> candidates;
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& entry = entries[i];
        if (kind != Kind::All && kind != entry.kind) continue;
        auto score = query.empty() ? std::optional<int>{0} : fuzzy::score(query, entry.title);
        if (!score) score = fuzzy::score(query, entry.identity + " " + entry.detail);
        if (score) candidates.push_back({entry, *score, i});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    std::vector<Entry> result;
    for (auto& candidate : candidates) result.push_back(std::move(candidate.entry));
    return result;
}

inline Page reflog(const std::string& repository, size_t offset, const std::string& query, std::stop_token stop) {
    Page page;
    std::vector<std::string> args{"log", "-g", "--all", "-100", "--skip=" + std::to_string(offset),
        "--date=iso-strict", "--format=%H%x00%gD%x00%gs%x00%gn%x00%ge"};
    if (!query.empty()) { args.push_back("--fixed-strings"); args.push_back("--regexp-ignore-case"); args.push_back("--grep-reflog=" + query); }
    args.push_back("--");
    auto result = git_run(repository, args, stop);
    if (!result.success()) { page.error = result.stderr_str(); return page; }
    for (const auto& line : fields(result.stdout_str(), '\n')) {
        auto f = fields(line, '\0');
        if (f.size() >= 5) page.entries.push_back({Kind::Reflog, f[1] + ":" + std::to_string(offset + page.entries.size()),
            f[2], f[1] + " · " + f[3] + " <" + f[4] + ">", f[0]});
    }
    page.more = page.entries.size() == 100;
    return page;
}

inline Page stashes(const std::string& repository, std::stop_token stop, size_t offset = 0) {
    Page page;
    auto result = git_run(repository, {"stash", "list", "-100", "--skip=" + std::to_string(offset), "--format=%H%x00%gd%x00%gs%x00%aI%x00%P"}, stop);
    if (!result.success()) { page.error = result.stderr_str(); return page; }
    for (const auto& line : fields(result.stdout_str(), '\n')) {
        auto f = fields(line, '\0');
        if (f.size() >= 5) page.entries.push_back({Kind::Stash, f[0], f[1] + " · " + f[2], f[3], f[0], false, f[4]});
    }
    page.more = page.entries.size() == 100;
    return page;
}

inline Page worktrees(const std::string& repository, std::stop_token stop) {
    Page page;
    auto result = git_run(repository, {"worktree", "list", "--porcelain", "-z"}, stop);
    if (!result.success()) { page.error = result.stderr_str(); return page; }
    Entry entry;
    auto finish = [&] {
        if (entry.identity.empty()) return;
        if (page.entries.size() >= 128) { page.more = true; return; }
        entry.kind = Kind::Worktree;
        if (entry.detail.empty()) entry.detail = "Detached HEAD";
        entry.title = std::filesystem::path(entry.identity).filename().string() + " · " + entry.detail;
        entry.detail = entry.identity;
        page.entries.push_back(std::move(entry));
        entry = {};
    };
    for (const auto& field : fields(result.stdout_str(), '\0')) {
        if (field.starts_with("worktree ")) { finish(); entry.identity = field.substr(9); }
        else if (field.starts_with("HEAD ")) entry.object = field.substr(5);
        else if (field.starts_with("branch refs/heads/")) entry.detail = field.substr(18);
        else if (field == "bare") entry.detail = "Bare repository";
        else if (field.starts_with("prunable")) entry.detail += " · missing/prunable";
        else if (field.starts_with("locked")) entry.detail += " · locked";
    }
    finish();
    return page;
}

inline std::string worktree_status(const std::string& path, std::stop_token stop) {
    auto result = git_run(path, {"status", "--porcelain=v2", "--untracked-files=normal", "-z"}, stop);
    if (!result.success()) return "Unavailable: " + result.stderr_str();
    size_t count = 0;
    const auto records = fields(result.stdout_str(), '\0');
    for (size_t i = 0; i < records.size(); ++i) {
        if (records[i].empty()) continue;
        const char kind = records[i].front();
        if (kind == '1' || kind == '2' || kind == 'u' || kind == '?') ++count;
        if (kind == '2' && i + 1 < records.size()) ++i;
    }
    return count == 0 ? "Clean" : std::to_string(count) + (count == 1 ? " changed file" : " changed files");
}

struct Changes {
    std::map<std::string, std::string> paths;
    std::string before;
    std::string error;
};

inline Changes changes(const std::string& repository, const std::string& revision, std::string before, std::stop_token stop) {
    Changes result;
    if (before.empty()) {
        auto parent = git_run(repository, {"rev-parse", "--verify", "--end-of-options", revision + "^1"}, stop);
        if (parent.success()) before = trim_line(parent.stdout_str());
    }
    result.before = before;
    std::vector<std::string> args;
    if (before.empty()) args = {"show", "--root", "--first-parent", "--format=", "--name-status", "-z", "--find-renames", revision, "--"};
    else args = {"diff", "--name-status", "-z", "--find-renames", before, revision, "--"};
    auto loaded = git_run(repository, args, stop);
    if (!loaded.success()) { result.error = loaded.stderr_str(); return result; }
    auto tokens = fields(loaded.stdout_str(), '\0');
    for (size_t i = 0; i + 1 < tokens.size();) {
        auto status = tokens[i++];
        while (status.starts_with('\n')) status.erase(0, 1);
        auto path = tokens[i++];
        if (status.starts_with('R') || status.starts_with('C')) {
            if (i >= tokens.size()) break;
            auto destination = tokens[i++];
            result.paths[destination] = "Renamed from " + path;
        } else result.paths[path] = status.starts_with('A') ? "Added" : status.starts_with('D') ? "Deleted" : "Modified";
    }
    return result;
}

inline Page diagnostics(const std::string& repository, std::stop_token stop) {
    Page page;
    std::string executable;
    if (const char* path = std::getenv("PATH")) for (const auto& directory : fields(path, ':')) {
        auto candidate = std::filesystem::path(directory) / "git";
        if (access(candidate.c_str(), X_OK) == 0) { executable = candidate.string(); break; }
    }
    page.entries.push_back({Kind::Diagnostic, "executable", "Git executable", executable, ""});
    auto version = git_run(repository, {"version", "--build-options"}, stop);
    if (!version.success()) { page.error = version.stderr_str(); return page; }
    size_t index = 0;
    for (const auto& line : fields(version.stdout_str(), '\n'))
        page.entries.push_back({Kind::Diagnostic, "version" + std::to_string(index++), line, "", ""});
    auto format = git_run(repository, {"rev-parse", "--show-object-format"}, stop);
    page.entries.push_back({Kind::Diagnostic, "object-format", "Object format", format.success() ? trim_line(format.stdout_str()) : "Unavailable", ""});
    auto commands = git_run(repository, {"help", "-a"}, stop);
    for (const auto& command : {"worktree", "range-diff", "restore", "switch", "sparse-checkout"}) {
        const bool available = commands.success() && commands.stdout_str().find(command) != std::string::npos;
        page.entries.push_back({Kind::Diagnostic, command, command, available ? "Available" : "Unavailable", ""});
    }
    return page;
}

inline Page submodules(const std::string& repository, std::stop_token stop) {
    Page page;
    auto result = git_run(repository, {"ls-files", "--stage", "-z"}, stop);
    if (!result.success()) { page.error = result.stderr_str(); return page; }
    for (const auto& record : fields(result.stdout_str(), '\0')) {
        if (!record.starts_with("160000 ")) continue;
        const auto tab = record.find('\t');
        if (tab == record.npos) continue;
        auto metadata = fields(std::string_view(record).substr(0, tab), ' ');
        if (metadata.size() < 3) continue;
        const auto path = record.substr(tab + 1);
        page.entries.push_back({Kind::Submodule, (std::filesystem::path(repository) / path).string(), path,
            "Gitlink " + metadata[1] + (metadata[2] == "0" ? "" : " · conflict stage " + metadata[2]), metadata[1]});
    }
    return page;
}

inline std::string submodule_status(const std::string& path, std::stop_token stop) {
    std::error_code error;
    const auto canonical = std::filesystem::canonical(path, error);
    if (error) return "Missing submodule directory";
    auto root = git_run(path, {"rev-parse", "--show-toplevel"}, stop);
    if (!root.success()) return "Submodule is not initialized";
    const auto actual = std::filesystem::canonical(trim_line(root.stdout_str()), error);
    if (error || actual != canonical) return "Submodule is not initialized";
    auto head = git_run(path, {"rev-parse", "--verify", "HEAD"}, stop);
    if (!head.success()) return "Submodule checkout unavailable";
    return "Checkout " + trim_line(head.stdout_str()) + " · " + worktree_status(path, stop);
}

}

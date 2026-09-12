#pragma once

#include "async_task.h"
#include <sstream>
#include <string>
#include <vector>

namespace codeowners {

struct Rule { std::string pattern; std::string owners; bool rooted = false; };
struct Document { std::vector<Rule> rules; std::string path; std::string error; };

inline bool matches(const Rule& rule, const std::string& path) {
    const auto& pattern = rule.pattern;
    std::vector<bool> current(path.size() + 1), next(path.size() + 1);
    current[0] = true;
    if (!rule.rooted)
        for (size_t i = 1; i <= path.size(); ++i) current[i] = path[i - 1] == '/';
    for (size_t i = 0; i < pattern.size(); ++i) {
        std::fill(next.begin(), next.end(), false);
        bool star = pattern[i] == '*';
        bool recursive = star && i + 1 < pattern.size() && pattern[i + 1] == '*';
        if (recursive) ++i;
        bool directories = recursive && i + 1 < pattern.size() && pattern[i + 1] == '/';
        if (directories) ++i;
        bool reachable = false;
        for (size_t j = 0; j <= path.size(); ++j) {
            if (star) {
                next[j] = current[j] || (j > 0 && (directories
                    ? reachable && path[j - 1] == '/'
                    : next[j - 1] && (recursive || path[j - 1] != '/')));
                reachable = reachable || current[j];
            } else if (j > 0) next[j] = current[j - 1] &&
                (pattern[i] == '?' ? path[j - 1] != '/' : path[j - 1] == pattern[i]);
        }
        current.swap(next);
    }
    for (size_t j = 0; j <= path.size(); ++j)
        if (current[j] && (j == path.size() || path[j] == '/')) return true;
    return false;
}

inline Document parse(const std::string& text, std::string path = {}) {
    Document out;
    out.path = std::move(path);
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.size() > 4096 || out.rules.size() >= 4096) {
            out.rules.clear();
            out.error = "CODEOWNERS exceeds the 4096-rule or 4096-byte line review limit";
            return out;
        }
        auto comment = line.find('#');
        if (comment != std::string::npos) line.resize(comment);
        std::istringstream fields(line);
        std::string pattern;
        if (!(fields >> pattern) || pattern.starts_with('!') || pattern.find_first_of("[]\\") != std::string::npos) continue;
        std::string owners, owner;
        while (fields >> owner) { if (!owners.empty()) owners += " "; owners += owner; }
        bool rooted = pattern.starts_with('/');
        if (rooted) pattern.erase(0, 1);
        if (pattern.ends_with('/')) pattern.pop_back();
        rooted = rooted || pattern.find('/') != std::string::npos;
        if (!pattern.empty()) out.rules.push_back({std::move(pattern), std::move(owners), rooted});
    }
    return out;
}

inline std::string owners_for(const Document& document, const std::string& path) {
    std::string owners;
    for (const auto& rule : document.rules)
        if (matches(rule, path)) owners = rule.owners;
    return owners;
}

async_work::Task<Document> load_async(const std::string& repo, const std::string& revision);

}

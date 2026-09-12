#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace review_files {

enum class Sort { Path, MostChanges, FewestChanges };

struct Filter {
    bool hideGenerated = false;
    bool hideVendor = false;
    bool hideLockfiles = false;
    std::string language;
    char change = ' ';
    Sort sort = Sort::Path;
    bool operator==(const Filter&) const = default;
};

inline std::string sort_label(Sort sort) {
    switch (sort) {
        case Sort::Path: return "Path";
        case Sort::MostChanges: return "Most changes";
        case Sort::FewestChanges: return "Fewest changes";
    }
    return {};
}

inline bool precedes(Sort sort, const std::string& left, int leftChanges, const std::string& right, int rightChanges) {
    if (sort != Sort::Path && leftChanges != rightChanges)
        return sort == Sort::MostChanges ? leftChanges > rightChanges : leftChanges < rightChanges;
    return left < right;
}

inline std::string language(std::string path) {
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const auto dot = path.find_last_of('.');
    const auto slash = path.find_last_of('/');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return "Text";
    auto ext = path.substr(dot);
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".hpp" || ext == ".h") return "C++";
    if (ext == ".c") return "C";
    if (ext == ".ts" || ext == ".tsx") return "TypeScript";
    if (ext == ".js" || ext == ".jsx") return "JavaScript";
    if (ext == ".py") return "Python";
    if (ext == ".rs") return "Rust";
    if (ext == ".go") return "Go";
    if (ext == ".swift") return "Swift";
    if (ext == ".md") return "Markdown";
    if (ext == ".json") return "JSON";
    if (ext == ".yaml" || ext == ".yml") return "YAML";
    if (ext == ".sh") return "Shell";
    if (ext == ".txt") return "Text";
    return ext;
}

inline std::string change_label(char change) {
    switch (change) {
        case 'A': return "Added";
        case 'M': return "Modified";
        case 'D': return "Deleted";
        case 'R': return "Renamed";
        default: return "All changes";
    }
}

inline bool matches(const Filter& filter, std::string path, char change = 'M') {
    if (!filter.language.empty() && filter.language != language(path)) return false;
    if (change == '?' || change == 'U') change = 'A';
    if (filter.change != ' ' && filter.change != change) return false;
    std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    auto name = std::string_view(path).substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
    auto segment = [&](std::string_view part) {
        return path.starts_with(std::string(part) + "/") || path.find("/" + std::string(part) + "/") != std::string::npos;
    };
    if (filter.hideVendor && (segment("vendor") || segment("node_modules") || segment("third_party"))) return false;
    if (filter.hideGenerated && (segment("generated") || segment("dist") || name.find(".generated.") != std::string_view::npos ||
        name.ends_with(".min.js") || name.ends_with(".min.css") || name.ends_with(".pb.cc") || name.ends_with(".pb.h"))) return false;
    if (filter.hideLockfiles && (name.ends_with(".lock") || name == "package-lock.json" || name == "pnpm-lock.yaml" ||
        name == "go.sum" || name == "npm-shrinkwrap.json")) return false;
    return true;
}

}

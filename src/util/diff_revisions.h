#pragma once

#include <string>
#include <utility>

struct DiffTarget {
    enum class Kind { WorkingTree, Index, File, Commit, Comparison, ParentComparison };
    Kind kind;
    std::string before;
    std::string after;
};

inline DiffTarget diff_target(const std::string& scope) {
    if (scope == "wt") return {DiffTarget::Kind::WorkingTree, "INDEX", ""};
    if (scope == "index") return {DiffTarget::Kind::Index, "HEAD", "INDEX"};
    if (scope.starts_with("file:")) return {DiffTarget::Kind::File, "", scope.substr(5)};
    if (scope.starts_with("parent:")) {
        auto separator = scope.find(':', 7);
        if (separator != std::string::npos)
            return {DiffTarget::Kind::ParentComparison, scope.substr(7, separator - 7), scope.substr(separator + 1)};
    }
    if (scope.starts_with("compare:")) {
        auto separator = scope.find(':', 8);
        if (separator != std::string::npos)
            return {DiffTarget::Kind::Comparison, scope.substr(8, separator - 8), scope.substr(separator + 1)};
    }
    return {DiffTarget::Kind::Commit, scope + "^", scope};
}

inline std::pair<std::string, std::string> diff_revisions(const std::string& scope) {
    auto target = diff_target(scope);
    return {target.before, target.after};
}

inline std::string diff_target_label(const std::string& scope) {
    const auto target = diff_target(scope);
    switch (target.kind) {
        case DiffTarget::Kind::WorkingTree: return "working tree (uncommitted)";
        case DiffTarget::Kind::Index: return "staged changes";
        case DiffTarget::Kind::File: return "file at " + target.after;
        case DiffTarget::Kind::Comparison: return "comparison " + target.before.substr(0, 12) + " → " + target.after.substr(0, 12);
        case DiffTarget::Kind::ParentComparison: return "merge " + target.after.substr(0, 12) + " against parent " + target.before.substr(0, 12);
        case DiffTarget::Kind::Commit: return "commit " + target.after;
    }
    return {};
}

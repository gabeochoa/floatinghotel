#pragma once

#include <string>
#include <vector>

namespace git {

struct HistoryScope {
    enum class Mode { Current, Branch, All };
    Mode mode = Mode::Current;
    std::string branch;
    bool remotes = false;
    std::string key() const { return std::to_string(static_cast<int>(mode)) + ":" + branch + ":" + std::to_string(remotes); }
    bool operator==(const HistoryScope&) const = default;
};

inline std::vector<std::string> history_scope_args(const HistoryScope& scope, size_t offset = 0) {
    std::vector<std::string> args{"log", "--topo-order", "-100", "--format=%H%x00%h%x00%s%x00%an%x00%aI%x00%D%x00%P", "--skip=" + std::to_string(offset)};
    if (scope.mode == HistoryScope::Mode::All) {
        if (!scope.remotes) args.push_back("--exclude=refs/remotes/*");
        args.push_back("--all");
    } else {
        args.push_back(scope.mode == HistoryScope::Mode::Current ? "HEAD" : scope.branch);
        if (scope.remotes) args.push_back("--remotes");
    }
    args.push_back("--");
    return args;
}

}

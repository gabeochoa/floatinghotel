#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace refresh_scope {

enum class Scope : unsigned {
    None = 0,
    Worktree = 1,
    Index = 2,
    Refs = 4,
    Full = 8,
};

struct Plan {
    bool status = false;
    bool log = false;
    bool diff = false;
    bool stagedDiff = false;
    bool branches = false;
    bool files = false;
};

inline Scope merge(Scope a, Scope b) {
    if (a == Scope::Full || b == Scope::Full) return Scope::Full;
    return static_cast<Scope>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

inline bool has(Scope value, Scope flag) {
    if (value == Scope::Full) return true;
    return (static_cast<unsigned>(value) & static_cast<unsigned>(flag)) != 0;
}

inline Plan plan(Scope scope) {
    if (scope == Scope::None || scope == Scope::Full)
        return {true, true, true, true, true, true};
    return {
        has(scope, Scope::Worktree) || has(scope, Scope::Index) || has(scope, Scope::Refs),
        has(scope, Scope::Refs),
        has(scope, Scope::Worktree) || has(scope, Scope::Index),
        has(scope, Scope::Index) || has(scope, Scope::Refs),
        has(scope, Scope::Refs),
        has(scope, Scope::Worktree) || has(scope, Scope::Index),
    };
}

inline std::string name(Scope scope) {
    if (scope == Scope::Full) return "full";
    std::string out;
    auto add = [&](Scope flag, std::string_view text) {
        if (!has(scope, flag)) return;
        if (!out.empty()) out += "+";
        out += text;
    };
    add(Scope::Worktree, "worktree");
    add(Scope::Index, "index");
    add(Scope::Refs, "refs");
    return out.empty() ? "none" : out;
}

inline std::filesystem::path normalized(const std::filesystem::path& path) {
    std::error_code error;
    auto weak = std::filesystem::weakly_canonical(path, error);
    if (!error && !weak.empty()) return weak;
    auto absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute.lexically_normal();
}

inline bool inside(const std::filesystem::path& child, const std::filesystem::path& parent) {
    auto c = normalized(child);
    auto p = normalized(parent);
    auto ci = c.begin();
    auto pi = p.begin();
    for (; pi != p.end(); ++pi, ++ci)
        if (ci == c.end() || *ci != *pi) return false;
    return true;
}

inline std::filesystem::path relative_to(const std::filesystem::path& child, const std::filesystem::path& parent) {
    auto c = normalized(child);
    auto p = normalized(parent);
    std::error_code error;
    auto relative = std::filesystem::relative(c, p, error);
    return error ? std::filesystem::path{} : relative;
}

inline Scope classify(const std::filesystem::path& repoRoot,
                      const std::filesystem::path& commonGitDir,
                      const std::filesystem::path& eventPath,
                      bool mustRescan) {
    if (mustRescan) return Scope::Full;
    if (!commonGitDir.empty() && inside(eventPath, commonGitDir)) {
        auto rel = relative_to(eventPath, commonGitDir);
        auto first = rel.empty() ? std::string() : rel.begin()->string();
        auto text = rel.string();
        if (text == "index" || text.starts_with("index.lock")) return Scope::Index;
        if (text == "HEAD" || text == "packed-refs" || first == "refs") return Scope::Refs;
        return Scope::Full;
    }
    if (inside(eventPath, repoRoot)) {
        auto rel = relative_to(eventPath, repoRoot);
        auto first = rel.empty() ? std::string() : rel.begin()->string();
        auto text = rel.string();
        if (first == ".git") {
            if (text == ".git/index" || text.starts_with(".git/index.lock")) return Scope::Index;
            if (text == ".git/HEAD" || text == ".git/packed-refs") return Scope::Refs;
            auto it = rel.begin();
            if (it != rel.end()) ++it;
            if (it != rel.end() && it->string() == "refs") return Scope::Refs;
            return Scope::Full;
        }
        return Scope::Worktree;
    }
    return Scope::Full;
}

}

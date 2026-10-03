#pragma once

#include <filesystem>
#include <string>

namespace ui::welcome {

// Stored recent paths are not always clean: opening a repo from the shell
// records forms like "/a/b/.", "/a/b/./" and "/a/b/". std::filesystem calls
// those filenames "." or "" , so the picker used to label real repositories
// "." with their parent as the path. Normalise before deriving either part.
inline std::filesystem::path normalized_repo_path(const std::string& stored) {
    if (stored.empty()) return {};
    std::filesystem::path path = std::filesystem::path(stored).lexically_normal();
    std::string text = path.string();
    while (text.size() > 1 && text.back() == '/') text.pop_back();
    return std::filesystem::path(text);
}

inline std::string repo_display_name(const std::string& stored) {
    const auto path = normalized_repo_path(stored);
    const std::string name = path.filename().string();
    if (!name.empty() && name != ".") return name;
    if (path == std::filesystem::path("/")) return "/";
    return stored.empty() ? std::string{} : stored;
}

inline std::string repo_display_parent(const std::string& stored,
                                       const std::string& home) {
    const auto path = normalized_repo_path(stored);
    std::string parent = path.parent_path().string();
    if (!home.empty() && (parent == home || parent.starts_with(home + "/")))
        parent = "~" + parent.substr(home.size());
    return parent;
}

inline std::string repo_initial(const std::string& name) {
    for (const char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
            return std::string(1, static_cast<char>(c >= 'a' && c <= 'z'
                                                        ? c - 'a' + 'A'
                                                        : c));
    }
    return "?";
}

}  // namespace ui::welcome

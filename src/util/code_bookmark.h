#pragma once

#include <string>

struct CodeBookmark {
    std::string path;
    std::string revision;
    int line = 1;
    std::string label;

    bool operator==(const CodeBookmark&) const = default;
};

inline std::string bookmark_display(const CodeBookmark& bookmark) {
    const auto location = bookmark.path + ":L" + std::to_string(bookmark.line);
    const auto label = bookmark.label.empty() || bookmark.label == location
        ? location : bookmark.label + " · " + location;
    return label + " @ " + (bookmark.revision.empty() ? "Working tree" :
        bookmark.revision == "INDEX" ? "Index" : bookmark.revision.substr(0, 7));
}

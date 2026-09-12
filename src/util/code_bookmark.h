#pragma once

#include <string>

struct CodeBookmark {
    std::string path;
    std::string revision;
    int line = 1;
    std::string label;

    bool operator==(const CodeBookmark&) const = default;
};

#pragma once

#include <fnmatch.h>
#include <sstream>
#include <string>
#include <vector>

inline bool path_glob_matches(const std::string& pattern, const std::string& path) {
    auto parts = [](const std::string& value) {
        std::vector<std::string> result;
        std::istringstream stream(value);
        std::string part;
        while (std::getline(stream, part, '/')) result.push_back(part);
        return result;
    };
    auto patterns = parts(pattern), segments = parts(path);
    std::vector<std::vector<bool>> match(patterns.size() + 1, std::vector<bool>(segments.size() + 1));
    match[0][0] = true;
    for (size_t i = 0; i < patterns.size(); ++i)
        for (size_t j = 0; j <= segments.size(); ++j) {
            if (!match[i][j]) continue;
            if (patterns[i] == "**") {
                match[i + 1][j] = true;
                if (j < segments.size()) match[i][j + 1] = true;
            } else if (j < segments.size() && fnmatch(patterns[i].c_str(), segments[j].c_str(), 0) == 0)
                match[i + 1][j + 1] = true;
        }
    return match.back().back();
}

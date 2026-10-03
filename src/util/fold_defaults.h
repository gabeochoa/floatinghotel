#pragma once

#include <string>
#include <vector>

#include "path_glob.h"

namespace fold_defaults {

// Per-repo rules for which files start folded when a commit is opened.
// Patterns are repository-relative globs (see path_glob.h), matched against
// the file's path in the commit. minChangedLines folds any file whose diff
// is at least that many changed lines (additions + deletions); 0 disables
// the threshold.
struct Rules {
    std::vector<std::string> patterns;
    int minChangedLines = 0;
};

// Shipped default for repos with no stored rules: test directories fold,
// everything else opens. A stored rule (even an empty pattern list)
// replaces this entirely.
inline Rules default_rules() { return {{"tests/**", "**/tests/**"}, 0}; }

inline bool matches(const Rules& rules, const std::string& path,
                    int changedLines) {
    if (rules.minChangedLines > 0 && changedLines >= rules.minChangedLines)
        return true;
    for (const auto& pattern : rules.patterns)
        if (!pattern.empty() && path_glob_matches(pattern, path)) return true;
    return false;
}

// "tests/**, *.generated.*" <-> {"tests/**", "*.generated.*"}; whitespace
// around entries is dropped, empty entries are skipped.
inline std::vector<std::string> parse_patterns(const std::string& text) {
    std::vector<std::string> result;
    std::string current;
    auto flush = [&] {
        const auto begin = current.find_first_not_of(" \t");
        const auto end = current.find_last_not_of(" \t");
        if (begin != std::string::npos)
            result.push_back(current.substr(begin, end - begin + 1));
        current.clear();
    };
    for (const char c : text) {
        if (c == ',') flush();
        else current += c;
    }
    flush();
    return result;
}

inline std::string format_patterns(const std::vector<std::string>& patterns) {
    std::string text;
    for (const auto& pattern : patterns) {
        if (!text.empty()) text += ", ";
        text += pattern;
    }
    return text;
}

}  // namespace fold_defaults

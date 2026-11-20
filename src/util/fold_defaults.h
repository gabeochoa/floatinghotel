#pragma once

#include <cctype>
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

// Noise classification for the global "fold generated / binary / test
// files" setting. Generated reuses the generated-file heuristics the file
// filter hides by; test paths cover test directories and the common
// test-file naming conventions.
inline bool is_generated_path(const std::string& rawPath) {
    std::string path = rawPath;
    for (auto& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto name = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
    auto segment = [&](const std::string& part) {
        return path.starts_with(part + "/") || path.find("/" + part + "/") != std::string::npos;
    };
    return segment("generated") || segment("generated_images") || segment("dist") ||
        name.find(".generated.") != std::string::npos || name.ends_with(".lock") ||
        name.ends_with(".min.js") || name.ends_with(".min.css") || name.ends_with(".pb.cc") || name.ends_with(".pb.h");
}

inline bool is_test_path(const std::string& rawPath) {
    std::string path = rawPath;
    for (auto& c : path) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const auto name = path.substr(path.find_last_of('/') == std::string::npos ? 0 : path.find_last_of('/') + 1);
    auto segment = [&](const std::string& part) {
        return path.starts_with(part + "/") || path.find("/" + part + "/") != std::string::npos;
    };
    if (segment("tests") || segment("test") || segment("__tests__")) return true;
    return name.starts_with("test_") || name.starts_with("test-") ||
        name.find("_test.") != std::string::npos || name.find("_tests.") != std::string::npos ||
        name.find(".test.") != std::string::npos || name.find(".spec.") != std::string::npos;
}

inline bool is_noise_file(const std::string& path, bool binary) {
    return binary || is_generated_path(path) || is_test_path(path);
}

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

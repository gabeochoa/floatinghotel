#pragma once

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace commit_message {

inline constexpr std::array<std::string_view, 10> kConventionalTypes{
    "feat", "fix", "docs", "refactor", "test", "perf", "build", "ci", "style", "chore"};

// A commit.template file as git presents it: '#' lines are instructions for
// the editor, not message text, and trailing blank lines are dropped.
inline std::string strip_template(std::string_view text) {
    std::string out;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        auto line = text.substr(start, end - start);
        if (!line.starts_with('#')) out.append(line).push_back('\n');
        start = end + 1;
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == ' ' || out.back() == '\r')) out.pop_back();
    return out;
}

// Length of a leading "type(scope)!: " on the subject line, or 0.
inline size_t prefix_length(std::string_view message) {
    size_t i = 0;
    while (i < message.size() && std::islower(static_cast<unsigned char>(message[i]))) ++i;
    if (i == 0) return 0;
    if (i < message.size() && message[i] == '(') {
        const size_t close = message.find(')', i);
        if (close == std::string_view::npos || message.substr(i, close - i).find('\n') != std::string_view::npos) return 0;
        i = close + 1;
    }
    if (i < message.size() && message[i] == '!') ++i;
    if (i >= message.size() || message[i] != ':') return 0;
    if (++i < message.size() && message[i] != ' ' && message[i] != '\n') return 0;  // "http://"
    while (i < message.size() && message[i] == ' ') ++i;
    return i;
}

// Put `type: ` at the start of the subject, replacing an existing
// conventional prefix (and its scope) but nothing after it.
inline std::string with_type(std::string_view message, std::string_view type) {
    return std::string(type) + ": " + std::string(message.substr(prefix_length(message)));
}

}  // namespace commit_message

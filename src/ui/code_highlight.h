#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
#include <utility>

namespace code_highlight {

enum class Kind { Plain, Keyword, String, Number, Comment };
using Range = std::pair<size_t, size_t>;

inline size_t display_size(std::string_view raw, bool visible) {
    size_t bytes = 0;
    for (const char ch : raw) bytes += ch == '\t' ? (visible ? 6 : 4) : ch == '\r' ? 0 : ch == ' ' && visible ? 2 : 1;
    return bytes;
}

inline std::string display_text(std::string_view raw, bool visible, bool ending = false, bool hasNewline = true) {
    std::string out;
    for (char ch : raw) {
        if (ch == '\t') out += visible ? "→   " : "    ";
        else if (ch == ' ' && visible) out += "·";
        else if (ch != '\r') out += ch;
    }
    if (visible && ending)
        out += !hasNewline ? " [no newline]" : raw.ends_with('\r') ? " [CRLF]" : " [LF]";
    return out;
}

inline std::pair<Range, Range> changed_ranges(std::string_view before, std::string_view after) {
    size_t prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) ++prefix;
    while (prefix > 0 && prefix < before.size() &&
           (static_cast<unsigned char>(before[prefix]) & 0xc0) == 0x80) --prefix;
    size_t a = before.size(), b = after.size();
    while (a > prefix && b > prefix && before[a - 1] == after[b - 1]) { --a; --b; }
    while (a < before.size() && (static_cast<unsigned char>(before[a]) & 0xc0) == 0x80) ++a;
    while (b < after.size() && (static_cast<unsigned char>(after[b]) & 0xc0) == 0x80) ++b;
    return {{prefix, a}, {prefix, b}};
}

inline std::vector<Range> hunk_ranges(const std::vector<std::string>& lines) {
    std::vector<Range> ranges(lines.size());
    size_t i = 0;
    while (i < lines.size()) {
        if (lines[i].empty() || lines[i].front() != '-') { ++i; continue; }
        size_t deleted = i;
        while (i < lines.size() && !lines[i].empty() && lines[i].front() == '-') ++i;
        size_t added = i;
        while (i < lines.size() && !lines[i].empty() && lines[i].front() == '+') ++i;
        for (size_t j = 0; j < std::min(added - deleted, i - added); ++j) {
            auto [oldRange, newRange] = changed_ranges(
                std::string_view(lines[deleted + j]).substr(1), std::string_view(lines[added + j]).substr(1));
            ranges[deleted + j] = oldRange;
            ranges[added + j] = newRange;
        }
    }
    return ranges;
}
struct Token {
    std::string text;
    Kind kind = Kind::Plain;
};

inline std::vector<Token> tokenize(const std::string& text, const std::string& path) {
    auto dot = path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot);
    static const std::unordered_set<std::string> supported = {
        ".c", ".h", ".cc", ".cpp", ".hpp", ".m", ".mm", ".js", ".jsx", ".ts", ".tsx",
        ".py", ".rs", ".go", ".java", ".kt", ".swift", ".sh", ".rb", ".json", ".yaml", ".yml", ".sql"};
    if (!supported.contains(ext)) return {{text, Kind::Plain}};
    static const std::unordered_set<std::string> keywords = {
        "alignas", "auto", "bool", "break", "case", "catch", "char", "class", "const", "constexpr",
        "continue", "default", "delete", "do", "double", "else", "enum", "explicit", "false", "float",
        "for", "if", "inline", "int", "long", "namespace", "new", "nullptr", "private", "protected",
        "public", "return", "short", "signed", "sizeof", "static", "struct", "switch", "template",
        "this", "throw", "true", "try", "typedef", "typename", "union", "unsigned", "using", "virtual",
        "void", "volatile", "while", "async", "await", "def", "elif", "except", "finally", "from",
        "import", "in", "is", "lambda", "None", "not", "or", "and", "pass", "raise", "self", "with",
        "yield", "False", "True", "export", "function", "let", "var", "null", "undefined", "interface",
        "type", "fn", "impl", "match", "mut", "pub", "trait", "use", "package", "func", "defer", "go",
        "range", "select", "chan", "map", "val", "fun", "override", "SELECT", "FROM", "WHERE", "JOIN"};
    bool hashComment = ext == ".py" || ext == ".sh" || ext == ".rb" || ext == ".yaml" || ext == ".yml";
    std::vector<Token> out;
    size_t i = 0;
    while (i < text.size()) {
        size_t start = i;
        Kind kind = Kind::Plain;
        unsigned char ch = static_cast<unsigned char>(text[i]);
        if ((hashComment && ch == '#') || text.compare(i, 2, "//") == 0 ||
            (ext == ".sql" && text.compare(i, 2, "--") == 0)) {
            out.push_back({text.substr(i), Kind::Comment});
            break;
        }
        if (text.compare(i, 2, "/*") == 0) {
            auto end = text.find("*/", i + 2);
            i = end == std::string::npos ? text.size() : end + 2;
            kind = Kind::Comment;
        } else if (ch == '"' || ch == '\'' || ch == '`') {
            ++i;
            while (i < text.size()) {
                if (text[i] == '\\' && i + 1 < text.size()) { i += 2; continue; }
                if (text[i++] == ch) break;
            }
            kind = Kind::String;
        } else if (std::isdigit(ch)) {
            ++i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '.')) ++i;
            kind = Kind::Number;
        } else if (std::isalpha(ch) || ch == '_') {
            ++i;
            while (i < text.size() && (std::isalnum(static_cast<unsigned char>(text[i])) || text[i] == '_')) ++i;
            if (keywords.contains(text.substr(start, i - start))) kind = Kind::Keyword;
        } else {
            ++i;
        }
        if (!out.empty() && out.back().kind == kind) out.back().text += text.substr(start, i - start);
        else out.push_back({text.substr(start, i - start), kind});
    }
    return out;
}

}

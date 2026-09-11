#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace code_highlight {

enum class Kind { Plain, Keyword, String, Number, Comment };
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

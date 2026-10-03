#pragma once

// Text-based document outline for Go to Symbol, without a language server.
// Comments and strings are masked with the code lexer first; what remains is
// matched with per-language heuristics, so results are approximate.

#include "code_lexer.h"
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace symbol_outline {

struct Symbol {
    std::string name;
    std::string kind;
    int line = 1;
    bool operator==(const Symbol&) const = default;
};

namespace detail {

inline bool ident(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || u == '_' || u == '$' || u >= 128;
}

inline bool control_word(std::string_view word) {
    static constexpr std::string_view words[] = {"if", "for", "while", "switch", "catch", "return", "sizeof", "function",
        "do", "else", "try", "decltype", "alignof", "static_assert", "new", "delete", "throw", "typeof", "await",
        "match", "foreach", "using", "defined", "func", "fn", "elif", "with", "lock", "synchronized"};
    return std::find(std::begin(words), std::end(words), word) != std::end(words);
}

inline bool type_word(std::string_view word) {
    static constexpr std::string_view words[] = {"class", "struct", "union", "enum", "namespace", "interface", "trait",
        "impl", "object", "record", "module"};
    return std::find(std::begin(words), std::end(words), word) != std::end(words);
}

inline size_t skip_space(std::string_view s, size_t at) {
    while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
    return at;
}

inline std::string_view word_at(std::string_view s, size_t at) {
    size_t end = at;
    while (end < s.size() && ident(s[end])) ++end;
    return s.substr(at, end - at);
}

// Skips a balanced <...> group starting at `at`; returns `at` if there is none.
inline size_t skip_angles(std::string_view s, size_t at) {
    if (at >= s.size() || s[at] != '<') return at;
    int depth = 0;
    for (size_t i = at; i < s.size(); ++i) {
        if (s[i] == '<') ++depth;
        else if (s[i] == '>' && --depth == 0) return i + 1;
        else if (s[i] == '{' || s[i] == ';') break;
    }
    return at;
}

// The qualified name (a::b, ~T, obj.m) ending at `end`, skipping generics.
inline std::pair<size_t, std::string> name_before(std::string_view s, size_t end) {
    while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    if (end > 0 && s[end - 1] == '>') {
        int depth = 0;
        while (end > 0) {
            const char c = s[--end];
            if (c == '>') ++depth;
            else if (c == '<' && --depth == 0) break;
        }
        while (end > 0 && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    }
    size_t begin = end;
    while (begin > 0) {
        if (ident(s[begin - 1]) || s[begin - 1] == '~') --begin;
        else if (begin > 1 && s[begin - 1] == ':' && s[begin - 2] == ':' && begin - 2 > 0 && ident(s[begin - 3])) begin -= 2;
        else if (s[begin - 1] == '.' && begin > 1 && ident(s[begin - 2])) --begin;
        else break;
    }
    return {begin, std::string(s.substr(begin, end - begin))};
}

struct Statement {
    std::string text;
    std::vector<int> lines;  // source line of each byte of text
    void push(char c, int line) { text += c; lines.push_back(line); }
    void clear() { text.clear(); lines.clear(); }
};

// A brace-language statement ending in "{": a type, a JS arrow binding, or a
// function definition. Calls, control flow, and initializers are rejected.
inline void classify(const Statement& statement, std::vector<Symbol>& out) {
    std::string_view s = statement.text;
    size_t start = skip_space(s, 0);
    if (s.substr(start).starts_with("template")) start = skip_space(s, skip_angles(s, skip_space(s, start + 8)));
    // Types: the first type keyword before any "(" or "=".
    for (size_t at = start; at < s.size() && s[at] != '(' && s[at] != '=';) {
        if (!ident(s[at])) { ++at; continue; }
        const auto word = word_at(s, at);
        if (at > 0 && (ident(s[at - 1]) || s[at - 1] == '.')) { at += word.size(); continue; }
        at += word.size();
        if (!type_word(word)) continue;
        size_t next = skip_space(s, at);
        if (word == "enum" && (word_at(s, next) == "class" || word_at(s, next) == "struct")) next = skip_space(s, next + 6);
        auto name = word_at(s, next);
        while (!name.empty() && next + name.size() + 1 < s.size() && s.substr(next + name.size(), 2) == "::") {
            const auto more = word_at(s, next + name.size() + 2);
            if (more.empty()) break;
            name = s.substr(next, name.size() + 2 + more.size());
        }
        if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) return;
        // "struct Foo* make()" is a function; "class Foo(val x)" (Kotlin) is not.
        const size_t after = skip_space(s, next + name.size());
        if (s.find('(', after) != std::string_view::npos && (after >= s.size() || s[after] != '(')) break;
        out.push_back({std::string(name), std::string(word), statement.lines[next]});
        return;
    }
    // JS/TS: [export] const|let|var NAME = (...) => {  /  = function (...) {
    {
        size_t at = start;
        if (word_at(s, at) == "export") at = skip_space(s, at + 6);
        const auto binding = word_at(s, at);
        if (binding == "const" || binding == "let" || binding == "var") {
            const size_t nameAt = skip_space(s, at + binding.size());
            const auto name = word_at(s, nameAt);
            if (!name.empty() && (s.find("=>") != std::string_view::npos || s.find("function") != std::string_view::npos))
                out.push_back({std::string(name), "function", statement.lines[nameAt]});
            return;
        }
    }
    // Functions: NAME(...) followed only by qualifiers, a return type, or a
    // constructor initializer list.
    size_t open = start;
    if (s.substr(start).starts_with("func (")) {  // Go method receiver
        int depth = 0;
        for (open = start + 5; open < s.size(); ++open) {
            if (s[open] == '(') ++depth;
            else if (s[open] == ')' && --depth == 0) { ++open; break; }
        }
    }
    int angles = 0;
    for (; open < s.size(); ++open) {
        const char c = s[open];
        if (c == '<') ++angles;
        else if (c == '>' && angles > 0 && (open == 0 || s[open - 1] != '-')) --angles;
        else if (c == '=' && angles == 0 && !(open > 0 && std::string_view("<>!=+-*/%&|^").find(s[open - 1]) != std::string_view::npos) &&
                 s.substr(0, open).find("operator") == std::string_view::npos) return;
        else if (c == '(' && angles == 0) break;
    }
    if (open >= s.size()) return;
    const auto [nameAt, name] = name_before(s, open);
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name.front()))) return;
    const auto last = name.substr(std::min(name.size(), name.find_last_of(":.") == std::string::npos ? 0 : name.find_last_of(":.") + 1));
    if (control_word(last) || control_word(name)) return;
    int depth = 0;
    size_t close = open;
    for (; close < s.size(); ++close) {
        if (s[close] == '(') ++depth;
        else if (s[close] == ')' && --depth == 0) break;
    }
    if (close >= s.size()) return;
    const auto tail = s.substr(skip_space(s, close + 1));
    static constexpr std::string_view allowed[] = {"const", "noexcept", "override", "final", "->", ":", "mutable",
        "throws", "where", "requires", "&", "volatile", "try"};
    if (!tail.empty() && std::none_of(std::begin(allowed), std::end(allowed), [&](auto prefix) { return tail.starts_with(prefix); }) &&
        !(ident(tail.front()) && s.substr(start).starts_with("func")))  // Go return types
        return;
    out.push_back({name, "function", statement.lines[nameAt]});
}

inline std::string heading_text(std::string_view line) {
    size_t at = 0;
    while (at < line.size() && at < 3 && line[at] == ' ') ++at;
    size_t hashes = 0;
    while (at + hashes < line.size() && line[at + hashes] == '#') ++hashes;
    if (hashes == 0 || hashes > 6 || (at + hashes < line.size() && line[at + hashes] != ' ' && line[at + hashes] != '\t')) return {};
    auto text = line.substr(at + hashes);
    while (!text.empty() && (text.back() == '#' || std::isspace(static_cast<unsigned char>(text.back())))) text.remove_suffix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    return text.empty() ? std::string{} : std::string(hashes, '#') + " " + std::string(text);
}

}  // namespace detail

inline bool markdown(std::string_view path) {
    return path.ends_with(".md") || path.ends_with(".markdown");
}

inline bool supported(std::string_view path) {
    const auto lang = code_lexer::language(path);
    return markdown(path) || lang == code_lexer::Language::C || lang == code_lexer::Language::Cpp ||
        lang == code_lexer::Language::Python || lang == code_lexer::Language::JavaScript || lang == code_lexer::Language::Slash;
}

// Scans `text` (the whole document) and returns its symbols in line order,
// at most `limit` of them.
inline std::vector<Symbol> scan(std::string_view path, std::string_view text, size_t limit = 5000) {
    std::vector<Symbol> out;
    if (!supported(path)) return out;
    const auto lang = code_lexer::language(path);
    const bool md = markdown(path), python = lang == code_lexer::Language::Python;
    const bool preprocessor = lang == code_lexer::Language::C || lang == code_lexer::Language::Cpp;
    code_lexer::State state;
    detail::Statement statement;
    bool fenced = false;
    int line = 1;
    for (size_t begin = 0; begin <= text.size() && out.size() < limit; ++line) {
        auto end = text.find('\n', begin);
        if (end == std::string_view::npos) end = text.size();
        auto raw = text.substr(begin, end - begin);
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        begin = end + 1;
        if (md) {
            const auto trimmed = raw.substr(detail::skip_space(raw, 0));
            if (trimmed.starts_with("```") || trimmed.starts_with("~~~")) fenced = !fenced;
            else if (!fenced) if (auto heading = detail::heading_text(raw); !heading.empty()) out.push_back({heading, "heading", line});
            if (end == text.size()) break;
            continue;
        }
        std::string code(raw.size(), ' ');
        for (size_t i = 0; i < raw.size(); ++i)
            if (code_lexer::advance_with_lookahead(state, lang, code_lexer::line_lookahead(raw, i, true)) == code_lexer::Region::Code) code[i] = raw[i];
        code_lexer::advance_with_lookahead(state, lang, "\n");
        if (python) {
            size_t at = detail::skip_space(code, 0);
            if (detail::word_at(code, at) == "async") at = detail::skip_space(code, at + 5);
            const auto word = detail::word_at(code, at);
            if (word == "def" || word == "class") {
                const auto name = detail::word_at(code, detail::skip_space(code, at + word.size()));
                if (!name.empty()) out.push_back({std::string(name), word == "def" ? "function" : "class", line});
            }
        } else if (preprocessor && code.substr(detail::skip_space(code, 0)).starts_with("#")) {
            // Directives are not declarations.
        } else {
            for (char c : code) {
                if (c == '{') { detail::classify(statement, out); statement.clear(); }
                else if (c == ';' || c == '}') statement.clear();
                else statement.push(c, line);
            }
            statement.push(' ', line);
            if (statement.text.size() > 16384) statement.clear();  // no declaration is this long
        }
        if (end == text.size()) break;
    }
    if (out.size() > limit) out.resize(limit);
    return out;
}

}  // namespace symbol_outline

#pragma once

#include "code_lexer.h"
#include "code_line.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace source_folding {

struct Range {
    int first = 1;
    int end = 1;
    bool operator==(const Range&) const = default;
    bool contains(int line) const { return first < line && line < end; }
};

struct State {
    static constexpr size_t limit = 512;
    std::uint64_t generation = 0;
    std::string identity;
    std::vector<Range> folded;

    void sync(const std::string& next) {
        if (identity == next) return;
        identity = next;
        folded.clear();
        ++generation;
    }
    bool closed(Range range) const { return std::find(folded.begin(), folded.end(), range) != folded.end(); }
    bool can_toggle(Range range) const { return folded.size() < limit || closed(range); }
    void reveal(int line) { if (std::erase_if(folded, [&](Range range) { return range.contains(line); })) ++generation; }
    void unfold() { if (!folded.empty()) { folded.clear(); ++generation; } }
    bool toggle(Range range) {
        auto found = std::find(folded.begin(), folded.end(), range);
        if (found != folded.end()) { folded.erase(found); ++generation; return true; }
        if (folded.size() >= limit) return false;
        folded.push_back(range);
        ++generation;
        return true;
    }
};

inline std::vector<Range> discover(std::string_view path, std::span<const reading::CodeLine> lines,
    code_lexer::State lexical = {}, bool atEnd = true) {
    const auto lang = code_lexer::language(path);
    const bool python = lang == code_lexer::Language::Python;
    if (!python && lang != code_lexer::Language::C && lang != code_lexer::Language::Cpp &&
        lang != code_lexer::Language::JavaScript && lang != code_lexer::Language::Json) return {};
    std::vector<Range> ranges;
    std::vector<int> braces;
    struct Suite { int first, indent; };
    std::vector<Suite> suites;
    std::optional<Suite> pending;
    int depth = 0;
    bool uncertain = false;
    for (const auto& line : lines) {
        const bool codeStart = lexical.mode == code_lexer::Mode::Code;
        const int initialDepth = depth;
        std::string code;
        int indent = 0;
        for (char ch : line.text) {
            if (ch == '\t' && python) { uncertain = true; break; }
            if (ch != ' ') break;
            ++indent;
        }
        if (uncertain) break;
        for (size_t i = 0; i < line.text.size(); ++i) {
            const char ch = line.text[i];
            const auto next = code_lexer::line_lookahead(line.text, i, true);
            const auto mode = lexical.mode;
            const auto region = code_lexer::advance_with_lookahead(lexical, lang, next);
            if (lang == code_lexer::Language::JavaScript &&
                ((region == code_lexer::Region::Code && ch == '/') ||
                 (mode == code_lexer::Mode::Template && next.starts_with("${")))) { uncertain = true; break; }
            if (region != code_lexer::Region::Code) continue;
            if (python) {
                code += ch;
                if (ch == '(' || ch == '[' || ch == '{') ++depth;
                if (ch == ')' || ch == ']' || ch == '}') --depth;
                if (depth < 0) { uncertain = true; break; }
            } else if (ch == '{') braces.push_back(line.column == 1 ? line.number : 0);
            else if (ch == '}' && !braces.empty()) {
                const int first = braces.back();
                braces.pop_back();
                if (first > 0 && line.number > first + 1) ranges.push_back({first, line.number});
            }
        }
        if (uncertain) break;
        code_lexer::advance_with_lookahead(lexical, lang, "\n");
        if (!python || !codeStart || initialDepth || line.column != 1 || code.find_first_not_of(" \r\t") == std::string::npos) continue;
        while (!suites.empty() && indent <= suites.back().indent) {
            if (line.number > suites.back().first + 1) ranges.push_back({suites.back().first, line.number});
            suites.pop_back();
        }
        if (pending && indent > pending->indent) suites.push_back(*pending);
        pending.reset();
        const auto start = code.find_first_not_of(' ');
        const auto end = code.find_last_not_of(" \r\t");
        const auto statement = std::string_view(code).substr(start);
        bool header = false;
        for (auto keyword : {"def ", "class ", "if ", "elif ", "else:", "for ", "while ", "with ",
                "try:", "except", "finally:", "async def ", "async for ", "async with ", "match ", "case "})
            header |= statement.starts_with(keyword);
        if (!depth && header && code[end] == ':') pending = Suite{line.number, indent};
    }
    if (python && atEnd && !uncertain && !depth && lexical.mode == code_lexer::Mode::Code && !lines.empty())
        for (const auto& suite : suites) if (lines.back().number > suite.first) ranges.push_back({suite.first, lines.back().number + 1});
    std::sort(ranges.begin(), ranges.end(), [](Range a, Range b) { return a.first < b.first || (a.first == b.first && a.end > b.end); });
    ranges.erase(std::unique(ranges.begin(), ranges.end(), [](Range a, Range b) { return a.first == b.first; }), ranges.end());
    return ranges;
}

}

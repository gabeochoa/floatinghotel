#pragma once

#include "../ecs/components.h"

namespace hunk_syntax {

inline std::pair<code_lexer::State, code_lexer::State> annotate(ecs::DiffHunk& hunk, const std::string& path,
    bool source = false, code_lexer::State before = {}, code_lexer::State after = {}, const std::string& beforePath = "") {
    const auto lang = code_lexer::language(path);
    const auto oldLang = code_lexer::language(beforePath.empty() ? path : beforePath);
    hunk.syntaxBefore.clear();
    hunk.syntaxAfter.clear();
    if (lang == code_lexer::Language::Plain && oldLang == code_lexer::Language::Plain) return {before, after};
    if (!source) hunk.syntaxBefore.reserve(hunk.lines.size());
    hunk.syntaxAfter.reserve(hunk.lines.size());
    for (size_t i = 0; i < hunk.lines.size(); ++i) {
        if (!source) hunk.syntaxBefore.push_back(before);
        hunk.syntaxAfter.push_back(after);
        const auto& line = hunk.lines[i];
        const char sign = line.empty() ? ' ' : line.front();
        const auto text = line.empty() ? std::string_view{} : std::string_view(line).substr(1);
        const bool newline = !hunk.noNewline.contains(i);
        if (!source && sign != '+') before = code_lexer::scan_line(text, newline, oldLang, before);
        if (sign != '-') after = code_lexer::scan_line(text, newline, lang, after);
    }
    return {source ? after : before, after};
}

inline code_lexer::State at(const ecs::DiffHunk& hunk, size_t index, bool before) {
    const auto& states = before && !hunk.syntaxBefore.empty() ? hunk.syntaxBefore : hunk.syntaxAfter;
    return index < states.size() ? states[index] : code_lexer::State{};
}

inline size_t owned_bytes(const ecs::DiffHunk& hunk) {
    return (hunk.syntaxBefore.capacity() + hunk.syntaxAfter.capacity()) * sizeof(code_lexer::State);
}

}

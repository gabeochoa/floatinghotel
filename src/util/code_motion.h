#pragma once

#include "code_words.h"
#include "code_line.h"
#include "code_position.h"
#include <span>

namespace reading {

enum class CodeMotion { Left, Right, Up, Down, WordLeft, WordRight, LineStart, LineEnd, DocumentStart, DocumentEnd };

inline int end_column(const CodeLine& line) {
    return line.column + column_at_byte(line.text, line.text.size()) - 1;
}

inline CodePosition move_code(CodePosition position, CodeMotion motion, std::span<const CodeLine> lines) {
    if (lines.empty()) return position;
    auto found = std::lower_bound(lines.begin(), lines.end(), position.line,
        [](const CodeLine& line, int number) { return line.number < number; });
    size_t index = found == lines.end() ? lines.size() - 1 : static_cast<size_t>(found - lines.begin());
    const auto& line = lines[index];
    const size_t byte = byte_at_column(line.text, position.column - line.column + 1);
    auto place = [&](size_t row, size_t at) {
        position.line = lines[row].number;
        position.column = lines[row].column + column_at_byte(lines[row].text, at) - 1;
        return position;
    };
    switch (motion) {
        case CodeMotion::DocumentStart: return place(0, 0);
        case CodeMotion::DocumentEnd: return place(lines.size() - 1, lines.back().text.size());
        case CodeMotion::LineStart: return place(index, 0);
        case CodeMotion::LineEnd: return place(index, line.text.size());
        case CodeMotion::Up:
        case CodeMotion::Down: {
            const size_t target = motion == CodeMotion::Up ? (index == 0 ? 0 : index - 1) : std::min(index + 1, lines.size() - 1);
            position.line = lines[target].number;
            position.column = std::clamp(position.column, lines[target].column, end_column(lines[target]));
            return position;
        }
        case CodeMotion::Left:
        case CodeMotion::WordLeft:
            if (byte == 0) return index == 0 ? place(index, 0) : place(index - 1, lines[index - 1].text.size());
            if (motion == CodeMotion::WordLeft) {
                size_t at = byte;
                while (at > 0 && std::isspace(static_cast<unsigned char>(line.text[at - 1]))) --at;
                return place(index, at == 0 ? 0 : word_at(line.text, at - 1).first);
            } else {
                const auto ends = code_wrap::character_ends(line.text);
                auto end = std::lower_bound(ends.begin(), ends.end(), byte);
                return place(index, end == ends.begin() ? 0 : *std::prev(end));
            }
        case CodeMotion::Right:
        case CodeMotion::WordRight:
            if (byte == line.text.size()) return index + 1 == lines.size() ? place(index, byte) : place(index + 1, 0);
            if (motion == CodeMotion::WordRight) {
                size_t at = byte;
                while (at < line.text.size() && std::isspace(static_cast<unsigned char>(line.text[at]))) ++at;
                return place(index, at == line.text.size() ? at : word_at(line.text, at).second);
            } else {
                const auto ends = code_wrap::character_ends(line.text);
                auto end = std::upper_bound(ends.begin(), ends.end(), byte);
                return place(index, end == ends.end() ? line.text.size() : *end);
            }
    }
    return position;
}

}

#pragma once

#include "reading_workspace.h"
#include "reading_anchor.h"

namespace reading {

inline std::optional<size_t> caret_byte(const CodePosition& caret, std::string_view path,
                                       DiffSide side, int line, std::string_view text,
                                       int firstColumn, bool finalFragment) {
    if (caret.path != path || caret.side != side || caret.line != line || caret.column < firstColumn) return {};
    const int endColumn = firstColumn + column_at_byte(text, text.size()) - 1;
    if (caret.column > endColumn || (!finalFragment && caret.column == endColumn)) return {};
    return byte_at_column(text, caret.column - firstColumn + 1);
}

}

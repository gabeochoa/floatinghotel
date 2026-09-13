#pragma once

#include <algorithm>
#include <string_view>

namespace reading {

inline int column_at_byte(std::string_view text, size_t byte) {
    int column = 1;
    for (size_t i = 0; i < std::min(byte, text.size()); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xc0) != 0x80) ++column;
    return column;
}

inline size_t byte_at_column(std::string_view text, int column) {
    int current = 1;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xc0) == 0x80) continue;
        if (current++ >= column) return i;
    }
    return text.size();
}

}

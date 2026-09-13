#pragma once

#include "code_wrap.h"
#include <cctype>
#include <utility>

namespace reading {

inline std::pair<size_t, size_t> word_at(std::string_view text, size_t byte) {
    if (text.empty()) return {0, 0};
    auto ends = code_wrap::character_ends(text);
    ends.insert(ends.begin(), 0);
    byte = std::min(byte, text.size() - 1);
    size_t index = static_cast<size_t>(std::upper_bound(ends.begin(), ends.end(), byte) - ends.begin() - 1);
    auto kind = [&](size_t at) {
        const auto c = static_cast<unsigned char>(text[ends[at]]);
        return std::isspace(c) ? 0 : std::isalnum(c) || c == '_' || c >= 128 ? 1 : 2;
    };
    const int category = kind(index);
    size_t first = index, last = index + 1;
    if (category != 2) {
        while (first > 0 && kind(first - 1) == category) --first;
        while (last + 1 < ends.size() && kind(last) == category) ++last;
    }
    return {ends[first], ends[last]};
}

}

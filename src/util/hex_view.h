#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hex_view {

struct Preview {
    std::vector<std::string> lines;
    size_t shown = 0;
    size_t loaded = 0;
    bool truncated = false;
};

inline std::pair<size_t, size_t> visible_rows(size_t count, float offset, float height, float rowHeight) {
    auto first = static_cast<size_t>(std::max(0.f, std::floor(offset / rowHeight) - 2.f));
    auto last = static_cast<size_t>(std::max(0.f, std::ceil((offset + height) / rowHeight) + 2.f));
    return {std::min(first, count), std::min(last, count)};
}

inline Preview make(std::string_view bytes, size_t offset = 0, size_t limit = 4096, size_t addressBase = 0) {
    Preview preview;
    preview.loaded = bytes.size();
    if (offset > bytes.size()) offset = bytes.size();
    size_t end = offset + std::min(bytes.size() - offset, limit);
    preview.shown = end - offset;
    preview.truncated = end < bytes.size();
    for (size_t base = offset; base < end; base += 16) {
        size_t count = std::min<size_t>(16, end - base);
        char address[2 * sizeof(size_t) + 1];
        std::snprintf(address, sizeof(address), "%08zx", addressBase + base);
        std::string line = address;
        line += "  ";
        for (size_t i = 0; i < 16; ++i) {
            if (i < count) {
                char cell[4];
                std::snprintf(cell, sizeof(cell), "%02x ", static_cast<unsigned char>(bytes[base + i]));
                line += cell;
            } else {
                line += "   ";
            }
            if (i == 7) line += " ";
        }
        line += " |";
        for (size_t i = 0; i < count; ++i) {
            unsigned char c = static_cast<unsigned char>(bytes[base + i]);
            line.push_back(std::isprint(c) ? static_cast<char>(c) : '.');
        }
        line += "|";
        preview.lines.push_back(std::move(line));
    }
    return preview;
}

inline std::string summary(const Preview& preview) {
    std::string text = "Hex preview: " + std::to_string(preview.shown) + " of " +
        std::to_string(preview.loaded) + " loaded bytes";
    if (preview.truncated) text += " (truncated)";
    return text;
}

}

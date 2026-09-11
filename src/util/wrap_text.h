#pragma once

#include <string>
#include <vector>

template <class Measure>
std::vector<std::string> wrap_measured_text(const std::string& text, float width, Measure measure) {
    std::vector<std::string> lines;
    std::string line;
    for (size_t i = 0; i < text.size();) {
        if (text[i] == '\n') { lines.push_back(std::move(line)); line.clear(); ++i; continue; }
        size_t end = i + 1;
        while (end < text.size() && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80) ++end;
        auto glyph = text.substr(i, end - i);
        if (!line.empty() && measure(line + glyph) > width) {
            lines.push_back(std::move(line));
            line.clear();
        }
        line += glyph;
        i = end;
    }
    lines.push_back(std::move(line));
    return lines;
}

#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include "wrap_text.h"
#include "reading_anchor.h"

namespace markdown_preview {

enum class Kind {
    Heading,
    Paragraph,
    ListItem,
    Code,
    Image,
    Blank,
};

struct Block {
    Kind kind = Kind::Paragraph;
    std::string text;
    int level = 0;
    int sourceLine = 1;
    int sourceColumn = 1;
};

inline std::string trim(std::string_view input) {
    size_t begin = 0;
    while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin]))) ++begin;
    size_t end = input.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1]))) --end;
    return std::string(input.substr(begin, end - begin));
}

inline bool is_markdown_path(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".md" || ext == ".markdown";
}

inline Block image_block(std::string_view line) {
    size_t altBegin = line.find('[');
    size_t altEnd = line.find(']', altBegin == std::string_view::npos ? 0 : altBegin + 1);
    std::string alt = altBegin != std::string_view::npos && altEnd != std::string_view::npos && altEnd > altBegin
        ? std::string(line.substr(altBegin + 1, altEnd - altBegin - 1))
        : "image";
    return {Kind::Image, "Image omitted: " + alt, 0};
}

inline std::vector<Block> parse(std::string_view text) {
    std::vector<Block> blocks;
    bool inCode = false;
    size_t start = 0;
    int sourceLine = 1;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(start, end - start);
        std::string stripped = trim(line);
        const size_t count = blocks.size();
        if (stripped.starts_with("```")) {
            inCode = !inCode;
        } else if (inCode) {
            blocks.push_back({Kind::Code, std::string(line), 0});
        } else if (stripped.empty()) {
            blocks.push_back({Kind::Blank, "", 0});
        } else if (stripped.starts_with("![")) {
            blocks.push_back(image_block(stripped));
        } else if (stripped.starts_with("#")) {
            size_t hashes = 0;
            while (hashes < stripped.size() && stripped[hashes] == '#') ++hashes;
            if (hashes <= 6 && hashes < stripped.size() && stripped[hashes] == ' ')
                blocks.push_back({Kind::Heading, trim(std::string_view(stripped).substr(hashes + 1)), static_cast<int>(hashes)});
            else
                blocks.push_back({Kind::Paragraph, stripped, 0});
        } else if (stripped.starts_with("- ") || stripped.starts_with("* ")) {
            blocks.push_back({Kind::ListItem, "• " + stripped.substr(2), 0});
        } else {
            blocks.push_back({Kind::Paragraph, stripped, 0});
        }
        if (blocks.size() != count) {
            auto& block = blocks.back();
            block.sourceLine = sourceLine;
            const auto begin = line.find(block.kind == Kind::ListItem ? stripped : block.text);
            block.sourceColumn = begin == std::string_view::npos ? 1 : reading::column_at_byte(std::string(line), begin);
        }
        ++sourceLine;
        if (end == text.size()) break;
        start = end + 1;
    }
    return blocks;
}

struct Line {
    std::string text;
    Kind kind;
    float fontSize;
    float height;
    int sourceLine;
    int sourceColumn;
};

struct Cache {
    std::string contentKey;
    std::vector<Block> blocks;
    std::vector<Line> lines;
    std::vector<float> offsets;
    float width = -1.f;
    float scale = -1.f;
    float codeFontSize = -1.f;
    float grid = -1.f;
};

template <class Measure>
std::vector<std::string> wrap_paragraph(const std::string& text, float width, Measure measure) {
    std::vector<std::string> lines;
    std::istringstream words(text);
    std::string line;
    for (std::string word; words >> word;) {
        auto candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && measure(candidate) > width) {
            lines.push_back(std::move(line));
            line.clear();
        }
        if (!line.empty()) line += " ";
        line += word;
        if (measure(line) > width) {
            auto pieces = wrap_measured_text(line, width, measure);
            for (size_t i = 0; i + 1 < pieces.size(); ++i) lines.push_back(std::move(pieces[i]));
            line = std::move(pieces.back());
        }
    }
    if (!line.empty() || lines.empty()) lines.push_back(std::move(line));
    return lines;
}

template <class Measure>
bool update(Cache& cache, const std::string& key, const std::string& text,
        float width, float scale, float codeFontSize, float grid, Measure measure) {
    if (cache.contentKey != key) {
        cache.blocks = parse(text);
        cache.contentKey = key;
        cache.width = -1.f;
    }
    if (cache.width == width && cache.scale == scale && cache.codeFontSize == codeFontSize && cache.grid == grid) return false;
    cache.width = width;
    cache.scale = scale;
    cache.codeFontSize = codeFontSize;
    cache.grid = grid;
    cache.lines.clear();
    cache.offsets = {0.f};
    for (const auto& block : cache.blocks) {
        float fontSize = std::max(14.f, block.kind == Kind::Code ? codeFontSize :
            (block.kind == Kind::Heading && block.level == 1 ? 16.f : 14.f) * scale);
        float height = std::ceil((block.kind == Kind::Blank ? 10.f * scale : fontSize * 1.5f) / grid) * grid;
        auto measureLine = [&](const std::string& line) { return measure(line, block.kind, fontSize); };
        auto lines = block.kind == Kind::Code ? wrap_measured_text(block.text, width, measureLine) :
            wrap_paragraph(block.text, width, measureLine);
        size_t cursor = 0;
        for (auto& line : lines) {
            if (block.kind != Kind::Code)
                while (cursor < block.text.size() && std::isspace(static_cast<unsigned char>(block.text[cursor]))) ++cursor;
            const int column = block.sourceColumn + reading::column_at_byte(block.text, cursor) - 1;
            for (const unsigned char byte : line) {
                if (block.kind != Kind::Code && std::isspace(byte)) continue;
                if (block.kind != Kind::Code)
                    while (cursor < block.text.size() && std::isspace(static_cast<unsigned char>(block.text[cursor]))) ++cursor;
                if (cursor < block.text.size()) ++cursor;
            }
            cache.lines.push_back({std::move(line), block.kind, fontSize, height, block.sourceLine, column});
            cache.offsets.push_back(cache.offsets.back() + height);
        }
    }
    return true;
}

inline std::pair<size_t, size_t> visible_rows(const Cache& cache, float offset, float height) {
    if (cache.lines.empty()) return {0, 0};
    auto begin = std::upper_bound(cache.offsets.begin(), cache.offsets.end(), std::max(0.f, offset - height));
    size_t first = begin == cache.offsets.begin() ? 0 : static_cast<size_t>(begin - cache.offsets.begin() - 1);
    auto end = std::lower_bound(cache.offsets.begin(), cache.offsets.end(), offset + height * 2.f);
    return {std::min(first, cache.lines.size()), std::min(static_cast<size_t>(end - cache.offsets.begin()), cache.lines.size())};
}

}

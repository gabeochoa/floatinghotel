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

// An in-repository link: a relative path and/or a #heading anchor.
struct Link {
    size_t begin = 0, end = 0;  // bytes of the block's (or line's) text
    std::string target;
};

struct Block {
    Kind kind = Kind::Paragraph;
    std::string text;
    int level = 0;
    int sourceLine = 1;
    int sourceColumn = 1;
    std::vector<Link> links;
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

// Replaces [label](target) with its label. Relative targets and anchors are
// recorded as links; web links keep only their label.
inline std::string strip_links(std::string_view text, std::vector<Link>& links, size_t offset = 0) {
    std::string out;
    for (size_t i = 0; i < text.size();) {
        const auto close = text[i] == '[' && (i == 0 || text[i - 1] != '!') ? text.find("](", i + 1) : std::string_view::npos;
        const auto end = close == std::string_view::npos ? close : text.find(')', close + 2);
        if (end == std::string_view::npos || text.substr(i + 1, close - i - 1).find('[') != std::string_view::npos) {
            out += text[i++];
            continue;
        }
        auto target = trim(text.substr(close + 2, end - close - 2));
        target = target.substr(0, target.find(' '));  // drop a "title"
        const auto begin = out.size();
        out += text.substr(i + 1, close - i - 1);
        if (!target.empty() && target.find("://") == std::string::npos && !target.starts_with("mailto:"))
            links.push_back({offset + begin, offset + out.size(), std::move(target)});
        i = end + 1;
    }
    return out;
}

// GitHub-style heading anchor: lowercase, spaces to '-', punctuation dropped.
inline std::string slug(std::string_view heading) {
    std::string out;
    for (const unsigned char c : heading) {
        if (std::isalnum(c) || c >= 128 || c == '-' || c == '_') out += static_cast<char>(std::tolower(c));
        else if (c == ' ') out += '-';
    }
    return out;
}

// The repository path a link in `from` points to ("/x" is repository-rooted);
// empty if it leaves the repository.
inline std::string resolve(std::string_view from, std::string_view path) {
    if (path.empty()) return std::string(from);
    const auto joined = path.starts_with('/') ? std::filesystem::path(path.substr(1))
                                              : std::filesystem::path(from).parent_path() / path;
    auto normal = joined.lexically_normal().generic_string();
    if (normal.empty() || normal == "." || normal.starts_with("..")) return {};
    return normal;
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
            std::vector<Link> links;
            if (hashes <= 6 && hashes < stripped.size() && stripped[hashes] == ' ') {
                auto text = strip_links(trim(std::string_view(stripped).substr(hashes + 1)), links);
                blocks.push_back({Kind::Heading, std::move(text), static_cast<int>(hashes), 1, 1, std::move(links)});
            } else {
                auto text = strip_links(stripped, links);
                blocks.push_back({Kind::Paragraph, std::move(text), 0, 1, 1, std::move(links)});
            }
        } else if (stripped.starts_with("- ") || stripped.starts_with("* ")) {
            std::vector<Link> links;
            auto text = "• " + strip_links(std::string_view(stripped).substr(2), links, std::string_view("• ").size());
            blocks.push_back({Kind::ListItem, std::move(text), 0, 1, 1, std::move(links)});
        } else {
            std::vector<Link> links;
            auto text = strip_links(stripped, links);
            blocks.push_back({Kind::Paragraph, std::move(text), 0, 1, 1, std::move(links)});
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
    std::vector<Link> links;  // bytes of `text`
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
            std::vector<Link> links;
            const Link* previous = nullptr;
            for (size_t i = 0; i < line.size(); ++i) {
                if (block.kind != Kind::Code && std::isspace(static_cast<unsigned char>(line[i]))) continue;
                if (block.kind != Kind::Code)
                    while (cursor < block.text.size() && std::isspace(static_cast<unsigned char>(block.text[cursor]))) ++cursor;
                const auto at = cursor;
                if (cursor < block.text.size()) ++cursor;
                const auto link = std::find_if(block.links.begin(), block.links.end(), [&](const Link& l) { return at >= l.begin && at < l.end; });
                if (link == block.links.end()) { previous = nullptr; continue; }
                if (previous == &*link) links.back().end = i + 1;  // spans the spaces between words too
                else links.push_back({i, i + 1, link->target});
                previous = &*link;
            }
            cache.lines.push_back({std::move(line), block.kind, fontSize, height, block.sourceLine, column, std::move(links)});
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

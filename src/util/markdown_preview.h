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
    Table,
    Blank,
};

// Code and tables keep their spacing and draw in the code font.
inline bool literal(Kind kind) { return kind == Kind::Code || kind == Kind::Table; }

enum class Style { Link, Bold, Italic, Code };

// A styled run of a block's (or line's) text. A link's target is an
// in-repository path and/or a #heading anchor.
struct Span {
    size_t begin = 0, end = 0;  // bytes of the block's (or line's) text
    Style style = Style::Link;
    std::string target;
};

struct Block {
    Kind kind = Kind::Paragraph;
    std::string text;
    int level = 0;  // heading depth; 1 marks a table's header rule
    int sourceLine = 1;
    int sourceColumn = 1;
    std::vector<Span> spans;
    std::vector<std::string> cells;  // table rows
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

inline bool web(std::string_view target) {
    return target.find("://") != std::string_view::npos || target.starts_with("mailto:");
}

// The target of "](target)" starting at `open`, without a "title".
inline std::string link_target(std::string_view text, size_t open, size_t end) {
    auto target = trim(text.substr(open, end - open));
    return target.substr(0, target.find(' '));
}

// Strips inline Markdown -- [label](target), **bold**, *em*, `code`, and
// backslash escapes -- recording styled spans. Web links keep only their label.
inline std::string inline_spans(std::string_view text, std::vector<Span>& spans, size_t offset = 0) {
    std::string out;
    const auto word = [&](size_t at) { return at < text.size() && std::isalnum(static_cast<unsigned char>(text[at])); };
    const auto styled = [&](std::string_view content, Style style, std::string target = {}) {
        const auto begin = out.size();
        out += content;
        spans.push_back({offset + begin, offset + out.size(), style, std::move(target)});
    };
    for (size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (c == '\\' && i + 1 < text.size() && std::ispunct(static_cast<unsigned char>(text[i + 1]))) {
            out += text[i + 1];
            i += 2;
            continue;
        }
        if (c == '`') {
            if (const auto close = text.find('`', i + 1); close != std::string_view::npos && close > i + 1) {
                styled(text.substr(i + 1, close - i - 1), Style::Code);
                i = close + 1;
                continue;
            }
        } else if (c == '[' && (i == 0 || text[i - 1] != '!')) {
            const auto close = text.find("](", i + 1);
            const auto end = close == std::string_view::npos ? close : text.find(')', close + 2);
            if (end != std::string_view::npos && text.substr(i + 1, close - i - 1).find('[') == std::string_view::npos) {
                auto target = link_target(text, close + 2, end);
                const auto label = text.substr(i + 1, close - i - 1);
                if (target.empty() || web(target)) out += label;
                else styled(label, Style::Link, std::move(target));
                i = end + 1;
                continue;
            }
        } else if (c == '*' || c == '_') {
            // Opens before a non-space; '_' only outside words (snake_case).
            const bool strong = i + 1 < text.size() && text[i + 1] == c;
            const auto marker = text.substr(i, strong ? 2 : 1);
            const size_t from = i + marker.size();
            if (from < text.size() && !std::isspace(static_cast<unsigned char>(text[from])) && (c == '*' || i == 0 || !word(i - 1))) {
                auto close = text.find(marker, from + 1);
                while (close != std::string_view::npos &&
                       (std::isspace(static_cast<unsigned char>(text[close - 1])) || (c == '_' && word(close + marker.size())) ||
                        (!strong && close + 1 < text.size() && text[close + 1] == c)))
                    close = text.find(marker, close + (strong ? 1 : 2));
                if (close != std::string_view::npos) {
                    styled(text.substr(from, close - from), strong ? Style::Bold : Style::Italic);
                    i = close + marker.size();
                    continue;
                }
            }
        }
        out += c;
        ++i;
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

// "![alt](src)": a local image becomes a link that opens it in the image
// viewer at the same revision; remote images are never fetched.
inline Block image_block(std::string_view line) {
    const auto close = line.find("](");
    const auto end = close == std::string_view::npos ? close : line.find(')', close + 2);
    std::string alt = close != std::string_view::npos && close > 2 ? std::string(line.substr(2, close - 2)) : "image";
    const auto target = end == std::string_view::npos ? std::string{} : link_target(line, close + 2, end);
    if (target.empty() || web(target)) return {Kind::Image, "Image omitted: " + alt, 0};
    Block block{Kind::Image, "Image: " + alt, 0};
    block.spans.push_back({std::string_view("Image: ").size(), block.text.size(), Style::Link, target});
    return block;
}

// "| a | b |" -> {"a", "b"}, inline formatting stripped.
inline std::vector<std::string> table_cells(std::string_view row) {
    if (row.starts_with('|')) row.remove_prefix(1);
    if (row.ends_with('|')) row.remove_suffix(1);
    std::vector<std::string> cells;
    for (size_t begin = 0;;) {
        auto end = row.find('|', begin);
        while (end != std::string_view::npos && end > 0 && row[end - 1] == '\\') end = row.find('|', end + 1);
        std::vector<Span> ignored;
        cells.push_back(inline_spans(trim(row.substr(begin, end == std::string_view::npos ? end : end - begin)), ignored));
        if (end == std::string_view::npos) return cells;
        begin = end + 1;
    }
}

// Pads each run of table rows to shared column widths (in code points; the
// table draws in the code font) and draws the header rule.
inline void layout_tables(std::vector<Block>& blocks) {
    for (size_t first = 0; first < blocks.size();) {
        if (blocks[first].kind != Kind::Table) { ++first; continue; }
        size_t last = first;
        std::vector<int> widths;
        const auto columns = [](const std::string& cell) { return reading::column_at_byte(cell, cell.size()) - 1; };
        for (; last < blocks.size() && blocks[last].kind == Kind::Table; ++last) {
            if (blocks[last].level == 1) continue;
            widths.resize(std::max(widths.size(), blocks[last].cells.size()));
            for (size_t c = 0; c < blocks[last].cells.size(); ++c) widths[c] = std::max(widths[c], columns(blocks[last].cells[c]));
        }
        for (size_t row = first; row < last; ++row) {
            auto& block = blocks[row];
            block.text.clear();
            for (size_t c = 0; c < (block.level == 1 ? widths.size() : block.cells.size()); ++c) {
                if (block.level == 1) {
                    if (c > 0) block.text += "─┼─";
                    for (int i = 0; i < widths[c]; ++i) block.text += "─";
                    continue;
                }
                if (c > 0) block.text += " │ ";
                block.text += block.cells[c] + std::string(static_cast<size_t>(widths[c] - columns(block.cells[c])), ' ');
            }
            while (block.text.ends_with(' ')) block.text.pop_back();
        }
        first = last;
    }
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
            std::vector<Span> spans;
            if (hashes <= 6 && hashes < stripped.size() && stripped[hashes] == ' ') {
                auto text = inline_spans(trim(std::string_view(stripped).substr(hashes + 1)), spans);
                blocks.push_back({Kind::Heading, std::move(text), static_cast<int>(hashes), 1, 1, std::move(spans)});
            } else {
                auto text = inline_spans(stripped, spans);
                blocks.push_back({Kind::Paragraph, std::move(text), 0, 1, 1, std::move(spans)});
            }
        } else if (stripped.starts_with('|')) {
            auto cells = table_cells(stripped);
            const bool rule = std::all_of(cells.begin(), cells.end(), [](const std::string& cell) {
                return cell.find('-') != std::string::npos && cell.find_first_not_of("-: ") == std::string::npos;
            });
            blocks.push_back({Kind::Table, stripped, rule ? 1 : 0, 1, 1, {}, std::move(cells)});
        } else if (stripped.starts_with("- ") || stripped.starts_with("* ")) {
            std::vector<Span> spans;
            auto text = "• " + inline_spans(std::string_view(stripped).substr(2), spans, std::string_view("• ").size());
            blocks.push_back({Kind::ListItem, std::move(text), 0, 1, 1, std::move(spans)});
        } else {
            std::vector<Span> spans;
            auto text = inline_spans(stripped, spans);
            blocks.push_back({Kind::Paragraph, std::move(text), 0, 1, 1, std::move(spans)});
        }
        if (blocks.size() != count) {
            auto& block = blocks.back();
            block.sourceLine = sourceLine;
            const auto begin = line.find(block.kind == Kind::ListItem || block.kind == Kind::Table ? stripped : block.text);
            block.sourceColumn = begin == std::string_view::npos ? 1 : reading::column_at_byte(std::string(line), begin);
        }
        ++sourceLine;
        if (end == text.size()) break;
        start = end + 1;
    }
    layout_tables(blocks);
    return blocks;
}

struct Line {
    std::string text;
    Kind kind;
    float fontSize;
    float height;
    int sourceLine;
    int sourceColumn;
    std::vector<Span> spans;  // bytes of `text`
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
        float fontSize = std::max(14.f, literal(block.kind) ? codeFontSize :
            (block.kind == Kind::Heading && block.level == 1 ? 16.f : 14.f) * scale);
        float height = std::ceil((block.kind == Kind::Blank ? 10.f * scale : fontSize * 1.5f) / grid) * grid;
        auto measureLine = [&](const std::string& line) { return measure(line, block.kind, fontSize); };
        auto lines = literal(block.kind) ? wrap_measured_text(block.text, width, measureLine) :
            wrap_paragraph(block.text, width, measureLine);
        size_t cursor = 0;
        for (auto& line : lines) {
            if (!literal(block.kind))
                while (cursor < block.text.size() && std::isspace(static_cast<unsigned char>(block.text[cursor]))) ++cursor;
            const int column = block.sourceColumn + reading::column_at_byte(block.text, cursor) - 1;
            std::vector<Span> spans;
            const Span* previous = nullptr;
            for (size_t i = 0; i < line.size(); ++i) {
                if (!literal(block.kind) && std::isspace(static_cast<unsigned char>(line[i]))) continue;
                if (!literal(block.kind))
                    while (cursor < block.text.size() && std::isspace(static_cast<unsigned char>(block.text[cursor]))) ++cursor;
                const auto at = cursor;
                if (cursor < block.text.size()) ++cursor;
                const auto span = std::find_if(block.spans.begin(), block.spans.end(), [&](const Span& s) { return at >= s.begin && at < s.end; });
                if (span == block.spans.end()) { previous = nullptr; continue; }
                if (previous == &*span) spans.back().end = i + 1;  // covers the spaces between words too
                else spans.push_back({i, i + 1, span->style, span->target});
                previous = &*span;
            }
            cache.lines.push_back({std::move(line), block.kind, fontSize, height, block.sourceLine, column, std::move(spans)});
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

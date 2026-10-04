#include "selection_copy.h"
#include "../util/reading_anchor.h"
#include <algorithm>
#include <cctype>
#include <tuple>

namespace git {

ecs::SelectionCopyResult copy_source_selection(FileRequest request, reading::CodeSelection selection,
                                               bool withLocation, std::stop_token stop) {
    ecs::SelectionCopyResult result;
    auto first = selection.anchor;
    auto last = selection.head;
    if (first.path != last.path || first.side != last.side || first.line < 1 || last.line < 1 ||
        first.column < 1 || last.column < 1) return {{}, "Selection endpoints do not identify one source"};
    if (std::tie(first.line, first.column) > std::tie(last.line, last.column)) std::swap(first, last);
    if (first == last) return result;
    if (withLocation) {
        result.text = first.path + ":L" + std::to_string(first.line);
        if (first.line != last.line) result.text += "-" + std::to_string(last.line);
        result.text += "\n";
    }
    request.page = {ecs::FilePageRequest::Action::TargetLine, {}, first.line, selection.sourceIdentity, 0, first.column};
    bool reachedFirst = false;
    auto append = [&](std::string_view value) {
        if (value.size() > selection_copy_limit - result.text.size()) {
            result.error = "Selection exceeds the 8 MiB copy limit";
            return false;
        }
        const size_t needed = result.text.size() + value.size();
        if (needed > result.text.capacity()) result.text.reserve(std::min(selection_copy_limit,
            std::max(needed, result.text.capacity() * 2)));
        result.text.append(value);
        return true;
    };
    while (!stop.stop_requested()) {
        auto content = read_file(request, stop);
        result.maxPageBytes = std::max(result.maxPageBytes, content.raw.size());
        if (!content.error.empty()) { result.error = std::move(content.error); break; }
        if (content.diff.isBinary) { result.error = "Cannot copy a text selection from a binary file"; break; }
        if (!content.resolvedRevision.empty()) request.revision = content.resolvedRevision;
        result.revision = request.revision;
        bool complete = false;
        for (const auto& hunk : content.diff.hunks) {
            int line = hunk.newStart;
            for (size_t index = 0; index < hunk.lines.size(); ++index, ++line) {
                if (line < first.line) continue;
                if (line > last.line) { result.error = "Selection no longer exists in this source"; break; }
                const auto text = std::string_view(hunk.lines[index]).substr(1);
                const int column = line == content.page.begin.line ? content.page.begin.column : 1;
                const int endColumn = column + reading::column_at_byte(text, text.size()) - 1;
                const bool continuation = line == content.page.next.line && content.page.next.continuation &&
                    content.page.next.offset < content.page.totalBytes;
                if (line == first.line && first.column >= column && first.column <= endColumn) reachedFirst = true;
                const auto begin = reading::byte_at_column(text, line == first.line ? std::max(1, first.column - column + 1) : 1);
                const auto end = line == last.line ? reading::byte_at_column(text, std::max(1, last.column - column + 1)) : text.size();
                if (end >= begin && !append(text.substr(begin, end - begin))) break;
                if (line == last.line && last.column <= endColumn) { complete = true; break; }
                if (!continuation && !hunk.noNewline.contains(index) && line < last.line && !append("\n")) break;
            }
            if (complete || !result.error.empty()) break;
        }
        if (!result.error.empty()) break;
        if (complete && reachedFirst) return result;
        if (content.page.next.offset >= content.page.totalBytes) {
            result.error = "Selection no longer exists in this source";
            break;
        }
        if (content.page.next.offset <= content.page.begin.offset) { result.error = "Copy could not advance through the file"; break; }
        request.page = {ecs::FilePageRequest::Action::Next, content.page.next, 0, content.page.sourceIdentity};
        request.detectedEncoding = content.page.encoding;
    }
    if (stop.stop_requested()) result.error = "Copy cancelled";
    result.text.clear();
    return result;
}

ecs::SelectionCopyResult copy_loaded_selection(const std::vector<reading::CodeLine>& lines,
                                               reading::CodeSelection selection, bool withLocation) {
    ecs::SelectionCopyResult result;
    auto first = selection.anchor;
    auto last = selection.head;
    if (first.path != last.path || first.side != last.side || first.line < 1 || last.line < 1 ||
        first.column < 1 || last.column < 1) return {{}, "Selection endpoints do not identify one source"};
    if (std::tie(first.line, first.column) > std::tie(last.line, last.column)) std::swap(first, last);
    if (first == last) return result;
    if (withLocation) {
        result.text = first.path + ":L" + std::to_string(first.line);
        if (first.line != last.line) result.text += "-" + std::to_string(last.line);
        result.text += "\n";
    }
    bool reachedFirst = false;
    for (const auto& line : lines) {
        if (line.number < first.line) continue;
        if (line.number > last.line) break;
        if (reachedFirst) result.text += '\n';
        reachedFirst = true;
        const auto begin = line.number == first.line ? reading::byte_at_column(line.text, std::max(1, first.column - line.column + 1)) : 0;
        const auto end = line.number == last.line ? reading::byte_at_column(line.text, std::max(1, last.column - line.column + 1)) : line.text.size();
        if (end > begin) result.text.append(line.text.substr(begin, end - begin));
        if (result.text.size() > selection_copy_limit) return {{}, "Selection exceeds the 8 MiB copy limit"};
        if (line.number == last.line) return result;
    }
    return {{}, "Selection no longer exists in this source"};
}

void make_snippet(ecs::SelectionCopyResult& result, const reading::CodeSelection& selection) {
    if (!result.error.empty()) return;
    auto first = selection.anchor.line, last = selection.head.line;
    if (first > last) std::swap(first, last);
    const auto& path = selection.anchor.path;
    std::string header = path + ":L" + std::to_string(first);
    if (last != first) header += "-" + std::to_string(last);
    header += result.revision.empty() ? " (working tree)" : result.revision == "INDEX" ? " (index)"
        : " @ " + result.revision.substr(0, 12);
    // The fence must outlast any backtick run inside the code.
    size_t run = 0, longest = 0;
    for (char c : result.text) longest = std::max(longest, run = c == '`' ? run + 1 : 0);
    const std::string fence(std::max<size_t>(3, longest + 1), '`');
    const auto name = path.substr(path.find_last_of('/') + 1);
    const auto dot = name.find_last_of('.');
    std::string language = dot == std::string::npos || dot == 0 ? "" : name.substr(dot + 1);
    for (auto& c : language) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string out = header + "\n" + fence + language + "\n" + result.text;
    if (!result.text.empty() && result.text.back() != '\n') out += "\n";
    out += fence + "\n";
    if (out.size() > selection_copy_limit) {
        result.text.clear();
        result.error = "Selection exceeds the 8 MiB copy limit";
        return;
    }
    result.text = std::move(out);
}

async_work::Task<ecs::SelectionCopyResult> copy_source_selection_async(FileRequest request,
    reading::CodeSelection selection, bool withLocation) {
    return async_work::launch([request = std::move(request), selection = std::move(selection), withLocation](std::stop_token stop) {
        return copy_source_selection(request, selection, withLocation, stop);
    }, async_work::Priority::Foreground, ecs::SelectionCopyResult{.error = "Copy is busy. Try again."});
}

}

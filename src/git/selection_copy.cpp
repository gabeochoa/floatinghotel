#include "selection_copy.h"
#include "../util/reading_anchor.h"
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

async_work::Task<ecs::SelectionCopyResult> copy_source_selection_async(FileRequest request,
    reading::CodeSelection selection, bool withLocation) {
    return async_work::launch([request = std::move(request), selection = std::move(selection), withLocation](std::stop_token stop) {
        return copy_source_selection(request, selection, withLocation, stop);
    }, async_work::Priority::Foreground, ecs::SelectionCopyResult{.error = "Copy is busy. Try again."});
}

}

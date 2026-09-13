#include "source_find.h"
#include "../util/reading_anchor.h"
#include "../util/file_page.h"

namespace git {

ecs::SourceFindResult find_source(FileRequest request, const std::string& query, std::stop_token stop) {
    ecs::SourceFindResult result;
    if (query.empty()) return result;
    if (query.size() > 65536) { result.error = "Find query exceeds 64 KiB"; return result; }
    if (query.find_first_of("\r\n") != std::string::npos) { result.error = "Find accepts a single line"; return result; }
    request.page = {};
    std::string tail;
    int tailColumn = 1;
    int lastMatchLine = 0, nextMatchColumn = 1;
    const int queryColumns = reading::column_at_byte(query, query.size()) - 1;
    while (!stop.stop_requested()) {
        auto content = read_file(request, stop);
        if (!content.error.empty()) { result.error = std::move(content.error); result.matches.clear(); return result; }
        if (content.diff.isBinary) { result.error = "Find is unavailable for binary files"; result.matches.clear(); return result; }
        if (!content.resolvedRevision.empty()) request.revision = content.resolvedRevision;
        result.sourceIdentity = content.page.sourceIdentity;
        result.scannedBytes = content.page.next.offset;
        result.maxPageBytes = std::max(result.maxPageBytes, content.raw.size());
        for (const auto& hunk : content.diff.hunks) {
            int line = hunk.newStart;
            for (const auto& encoded : hunk.lines) {
                if (stop.stop_requested()) break;
                const std::string_view text = std::string_view(encoded).substr(1);
                const auto prefix = tail.size();
                const int column = tail.empty() ? (line == content.page.begin.line ? content.page.begin.column : 1) : tailColumn;
                std::string combined = tail;
                combined.append(text);
                size_t at = 0, measured = 0;
                int matchColumn = column;
                while ((at = combined.find(query, at)) != std::string::npos) {
                    if (stop.stop_requested()) break;
                    matchColumn += reading::column_at_byte(std::string_view(combined).substr(measured, at - measured), at - measured) - 1;
                    measured = at;
                    if (line == lastMatchLine && matchColumn < nextMatchColumn) { ++at; continue; }
                    if (at + query.size() > prefix) {
                        if (result.matches.size() == 5000) { result.limited = true; return result; }
                        result.matches.push_back({line, matchColumn});
                        lastMatchLine = line;
                        nextMatchColumn = matchColumn + queryColumns;
                    }
                    at += query.size();
                }
                tail.clear();
                if (line == content.page.next.line && content.page.next.continuation && content.page.next.offset < content.page.totalBytes) {
                    auto begin = combined.size() > query.size() - 1 ? combined.size() - (query.size() - 1) : 0;
                    while (begin < combined.size() && (static_cast<unsigned char>(combined[begin]) & 0xc0) == 0x80) ++begin;
                    tail = combined.substr(begin);
                    tailColumn = column + reading::column_at_byte(combined, begin) - 1;
                }
                ++line;
            }
        }
        if (content.page.next.offset >= content.page.totalBytes) return result;
        if (content.page.next.offset <= content.page.begin.offset) { result.error = "Find could not advance through the file"; break; }
        request.page = {ecs::FilePageRequest::Action::Next, content.page.next, 0, content.page.sourceIdentity};
        request.detectedEncoding = content.page.encoding;
    }
    result.matches.clear();
    if (stop.stop_requested()) result.error = "Find cancelled";
    return result;
}

async_work::Task<ecs::SourceFindResult> find_source_async(FileRequest request, std::string query) {
    return async_work::launch([request = std::move(request), query = std::move(query)](std::stop_token stop) {
        return find_source(request, query, stop);
    }, async_work::Priority::Background, ecs::SourceFindResult{.error = "Find is busy. Try again."});
}

}

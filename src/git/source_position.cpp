#include "source_position.h"
#include "content_reader.h"
#include "../util/reading_anchor.h"

namespace git {

SourcePosition locate_source_position(const std::string& repository, reading::SourceLocation location, std::stop_token stop, const std::string& encoding) {
    SourcePosition result{std::move(location)};
    auto& target = result.location;
    auto content = read_file({repository, target.destination.path, reading::revision_text(target.destination.revision),
        {ecs::FilePageRequest::Action::TargetLine, {}, target.line, {}, 0, target.column}, encoding}, stop);
    if (!content.error.empty()) { result.error = std::move(content.error); return result; }
    if (content.diff.isBinary) { result.error = "Line navigation is unavailable for binary files"; return result; }
    if (!content.resolvedRevision.empty()) target.destination.revision = reading::ObjectId{std::move(content.resolvedRevision)};
    for (const auto& hunk : content.diff.hunks) {
        const int index = target.line - hunk.newStart;
        if (index < 0 || static_cast<size_t>(index) >= hunk.lines.size()) continue;
        std::string_view text = hunk.lines[static_cast<size_t>(index)];
        text.remove_prefix(std::min<size_t>(1, text.size()));
        if (text.ends_with('\r')) text.remove_suffix(1);
        const int lastColumn = reading::column_at_byte(text, text.size()) +
            (target.line == content.page.begin.line ? content.page.begin.column - 1 : 0);
        if (target.column > lastColumn && content.page.next.continuation && target.line == content.page.next.line && content.page.next.offset < content.page.totalBytes) {
            result.error = "Column is beyond this loaded line fragment";
            return result;
        }
        target.column = std::min(target.column, lastColumn);
        return result;
    }
    result.error = "This file has no selectable lines";
    return result;
}

async_work::Task<SourcePosition> locate_source_position_async(std::string repository, reading::SourceLocation location, std::string encoding) {
    auto rejected = SourcePosition{location, "Line navigation is busy. Try again."};
    return async_work::launch([repository = std::move(repository), location = std::move(location), encoding = std::move(encoding)](std::stop_token stop) {
        return locate_source_position(repository, location, stop, encoding);
    }, async_work::Priority::Foreground, std::move(rejected));
}

}

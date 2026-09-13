#pragma once

#include "content_reader.h"
#include "../util/hunk_syntax.h"

namespace git {

inline std::string read_syntax_seeds(FileRequest request, const std::vector<int>& targets,
    std::vector<code_lexer::State>& states, std::stop_token stop) {
    states.resize(targets.size());
    ecs::FullFileContent page;
    for (size_t i = 0; i < targets.size(); ++i) {
        const int line = targets[i];
        if (line <= 1 || code_lexer::language(request.path) == code_lexer::Language::Plain) continue;
        while (!page.page.contains(line, 1)) {
            if (stop.stop_requested()) return "Syntax load cancelled";
            if (!page.page.totalBytes) request.page = {ecs::FilePageRequest::Action::TargetLine, {}, line};
            else {
                if (page.page.next.offset >= page.page.totalBytes) return "Syntax location is no longer available";
                request.page = {ecs::FilePageRequest::Action::Next, page.page.next, 0, page.page.sourceIdentity};
                request.detectedEncoding = page.page.encoding;
            }
            const auto previous = page.page.next.offset;
            page = read_file(request, stop);
            if (!page.error.empty()) return page.error;
            if (page.diff.isBinary) return "Syntax is unavailable for binary files";
            if (page.page.next.offset <= previous) return "Syntax reader made no progress";
        }
        if (page.diff.hunks.empty()) return "Syntax location is no longer available";
        states[i] = hunk_syntax::at(page.diff.hunks.front(), line - page.page.begin.line, false);
    }
    return {};
}

inline ecs::DiffSyntaxResult read_diff_syntax(FileRequest before, FileRequest after,
    const std::vector<std::pair<int, int>>& starts, std::stop_token stop = {}) {
    ecs::DiffSyntaxResult result;
    std::vector<int> oldLines, newLines;
    for (const auto& [oldLine, newLine] : starts) { oldLines.push_back(oldLine); newLines.push_back(newLine); }
    std::vector<code_lexer::State> oldStates, newStates;
    result.error = read_syntax_seeds(before, oldLines, oldStates, stop);
    if (result.error.empty()) result.error = read_syntax_seeds(after, newLines, newStates, stop);
    if (!result.error.empty()) return result;
    for (size_t i = 0; i < starts.size(); ++i) result.seeds.emplace_back(oldStates[i], newStates[i]);
    return result;
}

}

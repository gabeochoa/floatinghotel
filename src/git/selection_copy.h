#pragma once

#include "content_reader.h"
#include "../util/code_line.h"

namespace git {

inline constexpr size_t selection_copy_limit = 8 * 1024 * 1024;
ecs::SelectionCopyResult copy_source_selection(FileRequest request, reading::CodeSelection selection,
                                               bool withLocation, std::stop_token stop = {});
async_work::Task<ecs::SelectionCopyResult> copy_source_selection_async(FileRequest request,
    reading::CodeSelection selection, bool withLocation);
// Copies from lines already in memory (a saved-review snapshot has no
// revision to reread). Same endpoint rules and limit as above.
ecs::SelectionCopyResult copy_loaded_selection(const std::vector<reading::CodeLine>& lines,
                                               reading::CodeSelection selection, bool withLocation);
// Rewrites a plain copy as "path:Lfirst-last @ revision" plus a fenced block,
// ready to paste into an agent prompt. Keeps the copy limit.
void make_snippet(ecs::SelectionCopyResult& result, const reading::CodeSelection& selection);

}

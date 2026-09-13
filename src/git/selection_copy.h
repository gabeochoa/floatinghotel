#pragma once

#include "content_reader.h"

namespace git {

inline constexpr size_t selection_copy_limit = 8 * 1024 * 1024;
ecs::SelectionCopyResult copy_source_selection(FileRequest request, reading::CodeSelection selection,
                                               bool withLocation, std::stop_token stop = {});
async_work::Task<ecs::SelectionCopyResult> copy_source_selection_async(FileRequest request,
    reading::CodeSelection selection, bool withLocation);

}

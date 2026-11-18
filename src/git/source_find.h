#pragma once

#include "content_reader.h"

namespace git {

ecs::SourceFindResult find_source(FileRequest request, const std::string& query, std::stop_token stop = {});
async_work::Task<ecs::SourceFindResult> find_source_async(FileRequest request, std::string query);
async_work::Task<ecs::OutlineResult> outline_source_async(FileRequest request);
async_work::Task<ecs::ProjectSymbolsResult> project_symbols_async(std::string repoPath,
    std::vector<std::string> paths, std::string query);

}

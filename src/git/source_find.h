#pragma once

#include "content_reader.h"

namespace git {

ecs::SourceFindResult find_source(FileRequest request, const std::string& query, std::stop_token stop = {});
async_work::Task<ecs::SourceFindResult> find_source_async(FileRequest request, std::string query);

}

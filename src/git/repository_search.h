#pragma once

#include "../ecs/components.h"

namespace git {

std::vector<std::string> repository_search_args(const ecs::SearchQuery& query);
std::vector<ecs::SearchMatch> parse_search_matches(const std::string& output, const std::string& revision);
async_work::Task<ecs::SearchResult> search_repository_async(ecs::SearchQuery query);

}

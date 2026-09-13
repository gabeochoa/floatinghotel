#pragma once

#include "git_runner.h"
#include "../util/reading_workspace.h"

namespace git {

struct PathList {
    reading::SourceRevision revision;
    std::vector<std::string> paths;
    std::string error;
    bool truncated = false;
};

PathList read_paths(const std::string& repository, reading::SourceRevision revision,
    std::stop_token stop = {}, size_t maxBytes = 4 * 1024 * 1024);
async_work::Task<PathList> load_paths_async(std::string repository, reading::SourceRevision revision);

}

#pragma once

#include "../ecs/components.h"

namespace git {

struct FileRequest {
    std::string repo;
    std::string path;
    std::string revision;
    ecs::FilePageRequest page;
    std::string encoding = "auto";
    std::string detectedEncoding;
};

ecs::FileDiff parse_complete_file(const std::string& path, const std::string& content);
ecs::FullFileContent read_file(const FileRequest& request, std::stop_token stop = {});
async_work::Task<ecs::FullFileContent> read_file_async(FileRequest request);

}

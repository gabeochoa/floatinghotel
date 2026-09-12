#pragma once

#include "../ecs/components.h"

namespace git {

struct FileRequest {
    std::string repo;
    std::string path;
    std::string revision;
};

ecs::FileDiff parse_complete_file(const std::string& path, const std::string& content);
ecs::FullFileContent read_file(const FileRequest& request);
std::future<ecs::FullFileContent> read_file_async(FileRequest request);

}

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

ecs::FileDiff parse_complete_file(const std::string& path, const std::string& content, code_lexer::State incoming = {});
ecs::FullFileContent read_file(const FileRequest& request, std::stop_token stop = {});
async_work::Task<ecs::FullFileContent> read_file_async(FileRequest request);
// The cached page for a path at a full commit id, using the tree entry its
// last validated read found; no git process. Callers still run read_file and
// take its answer when it disagrees (missing path, changed blob).
std::optional<ecs::FullFileContent> peek_file(const FileRequest& request);
ecs::UntrackedReviewFiles read_untracked_review_files(const std::string& repo, const std::vector<std::string>& paths,
                                                     std::stop_token stop = {});
async_work::Task<ecs::UntrackedReviewFiles> read_untracked_review_files_async(std::string repo, std::vector<std::string> paths);

}

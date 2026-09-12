#include "content_reader.h"

#include <filesystem>
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>

namespace git {

ecs::FileDiff parse_complete_file(const std::string& path, const std::string& content) {
    ecs::FileDiff file;
    file.filePath = path;
    file.isFullContent = true;
    file.isBinary = content.find('\0') != std::string::npos;
    if (file.isBinary) return file;
    ecs::DiffHunk hunk;
    hunk.oldStart = hunk.newStart = 1;
    hunk.header = "Complete file";
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) hunk.lines.push_back(" " + line);
    hunk.oldCount = hunk.newCount = static_cast<int>(hunk.lines.size());
    if (!content.empty() && !content.ends_with('\n')) hunk.noNewline.insert(hunk.lines.size() - 1);
    file.hunks.push_back(std::move(hunk));
    return file;
}

ecs::FullFileContent read_file(const FileRequest& request, std::stop_token stop) {
    ecs::FullFileContent content;
    if (stop.stop_requested()) { content.error = "File load cancelled"; return content; }
    if (request.revision.empty()) {
        std::ifstream input(std::filesystem::path(request.repo) / request.path, std::ios::binary);
        if (!input) content.error = "Unable to read working-tree file";
        else {
            std::array<char, 65536> buffer;
            while (input && !stop.stop_requested()) {
                input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                content.raw.append(buffer.data(), static_cast<size_t>(input.gcount()));
            }
            if (input.bad()) content.error = "Unable to finish reading working-tree file";
        }
    } else {
        std::string spec = request.revision == "INDEX" ? ":" + request.path
                          : request.revision + ":" + request.path;
        auto result = git_run(request.repo, {"show", spec}, stop);
        if (result.success()) content.raw = std::move(result.raw.stdout_str);
        else content.error = result.stderr_str();
    }
    if (stop.stop_requested()) content.error = "File load cancelled";
    if (content.error.empty()) content.diff = parse_complete_file(request.path, content.raw);
    return content;
}

async_work::Task<ecs::FullFileContent> read_file_async(FileRequest request) {
    return async_work::launch([request = std::move(request)](std::stop_token stop) {
        return read_file(request, stop);
    }, async_work::Priority::Foreground, ecs::FullFileContent{{}, {}, "Background queue is full; reopen the file to retry"});
}

}

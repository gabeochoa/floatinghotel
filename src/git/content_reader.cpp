#include "content_reader.h"
#include "../util/file_content.h"
#include "../util/text_decode.h"

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
    std::string mode;
    if (stop.stop_requested()) { content.error = "File load cancelled"; return content; }
    if (request.revision.empty()) {
        auto result = file_content::read_working_file(std::filesystem::path(request.repo) / request.path, stop);
        content.raw = std::move(result.bytes);
        mode = std::move(result.mode);
        content.error = std::move(result.error);
    } else {
        std::string spec = request.revision == "INDEX" ? ":" + request.path
                          : request.revision + ":" + request.path;
        auto result = git_run(request.repo, {"show", spec}, stop);
        if (result.success()) content.raw = std::move(result.raw.stdout_str);
        else content.error = result.stderr_str();
        if (content.error.empty()) {
            auto modes = git_run(request.repo, request.revision == "INDEX"
                ? std::vector<std::string>{"ls-files", "--stage", "-z", "--", ":(literal)" + request.path}
                : std::vector<std::string>{"ls-tree", "-z", request.revision, "--", ":(literal)" + request.path}, stop);
            if (modes.success()) mode = file_content::mode_for_path(modes.stdout_str(), request.path);
            else content.error = modes.stderr_str();
        }
    }
    if (stop.stop_requested()) content.error = "File load cancelled";
    if (content.error.empty()) {
        auto decoded = text_decode::decode(content.raw, request.encoding);
        content.encodingLabel = decoded.encoding;
        content.diff = parse_complete_file(request.path, decoded.text);
        content.diff.isBinary = decoded.binary;
        content.diff.oldMode = content.diff.newMode = mode;
    }
    return content;
}

async_work::Task<ecs::FullFileContent> read_file_async(FileRequest request) {
    return async_work::launch([request = std::move(request)](std::stop_token stop) {
        return read_file(request, stop);
    }, async_work::Priority::Foreground, ecs::FullFileContent{{}, {}, "Background queue is full; reopen the file to retry"});
}

}

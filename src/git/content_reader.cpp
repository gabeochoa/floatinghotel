#include "content_reader.h"

#include <filesystem>
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

ecs::FullFileContent read_file(const FileRequest& request) {
    ecs::FullFileContent content;
    if (request.revision.empty()) {
        std::ifstream input(std::filesystem::path(request.repo) / request.path, std::ios::binary);
        if (!input) content.error = "Unable to read working-tree file";
        else {
            content.raw.assign(std::istreambuf_iterator<char>(input), {});
            if (input.bad()) content.error = "Unable to finish reading working-tree file";
        }
    } else {
        std::string spec = request.revision == "INDEX" ? ":" + request.path
                          : request.revision + ":" + request.path;
        auto result = git_run(request.repo, {"show", spec});
        if (result.success()) content.raw = std::move(result.raw.stdout_str);
        else content.error = result.stderr_str();
    }
    if (content.error.empty()) content.diff = parse_complete_file(request.path, content.raw);
    return content;
}

std::future<ecs::FullFileContent> read_file_async(FileRequest request) {
    std::packaged_task<ecs::FullFileContent()> task([request = std::move(request)] {
        return read_file(request);
    });
    auto result = task.get_future();
    std::thread(std::move(task)).detach();
    return result;
}

}

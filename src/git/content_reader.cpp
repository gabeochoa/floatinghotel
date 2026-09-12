#include "content_reader.h"
#include "blob_page_cache.h"
#include "../util/file_content.h"
#include "../util/file_page.h"
#include "../util/markdown_preview.h"
#include "../../vendor/afterhours/src/logging.h"

#include <filesystem>
#include <array>
#include <fstream>
#include <iterator>
#include <sstream>
#include <thread>
#include <charconv>

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

ecs::FullFileContent read_file(const FileRequest& input, std::stop_token stop) {
    FileRequest request = input;
    ecs::FullFileContent content;
    if (!request.revision.empty() && request.revision != "INDEX") {
        if (!reading::is_object_id(request.revision)) {
            auto resolved = git_run(request.repo, {"rev-parse", "--verify", "--end-of-options", request.revision + "^{commit}"}, stop);
            if (!resolved.success()) { content.error = resolved.stderr_str(); return content; }
            request.revision = resolved.stdout_str();
            while (!request.revision.empty() && (request.revision.back() == '\n' || request.revision.back() == '\r')) request.revision.pop_back();
        }
        content.resolvedRevision = request.revision;
    }
    auto& cache = blob_page_cache();
    std::string cacheKey;
    bool cacheHit = false;
    std::string mode;
    if (stop.stop_requested()) { content.error = "File load cancelled"; return content; }
    auto offset = request.page.action == ecs::FilePageRequest::Action::Next ? request.page.cursor.offset : 0;
    file_page::Collector collector(request.page, request.encoding, request.detectedEncoding, request.revision.empty() ? offset : 0);
    auto consume = [&](std::string_view bytes) { return !stop.stop_requested() && collector.consume(bytes); };
    if (request.revision.empty()) {
        auto result = file_content::read_working_file(std::filesystem::path(request.repo) / request.path, stop,
            std::numeric_limits<size_t>::max(), consume, offset);
        mode = std::move(result.mode);
        content.error = std::move(result.error);
        content.page.totalBytes = result.totalBytes;
        content.page.sourceIdentity = std::move(result.identity);
    } else {
        std::string spec = request.revision == "INDEX" ? ":" + request.path
                          : request.revision + ":" + request.path;
        auto resolved = git_run(request.repo, {"rev-parse", "--verify", "--end-of-options", spec}, stop);
        if (resolved.success()) {
            content.page.blob = resolved.stdout_str();
            while (!content.page.blob.empty() && (content.page.blob.back() == '\n' || content.page.blob.back() == '\r')) content.page.blob.pop_back();
        } else content.error = resolved.stderr_str();
        if (content.error.empty()) {
            auto measured = git_run(request.repo, {"cat-file", "-s", content.page.blob}, stop);
            auto text = measured.stdout_str();
            auto parsed = std::from_chars(text.data(), text.data() + text.size(), content.page.totalBytes);
            if (!measured.success() || parsed.ec != std::errc{}) content.error = "Unable to measure file revision";
        }
        if (content.error.empty()) {
            auto modes = git_run(request.repo, request.revision == "INDEX"
                ? std::vector<std::string>{"ls-files", "--stage", "-z", "--", ":(literal)" + request.path}
                : std::vector<std::string>{"ls-tree", "-z", request.revision, "--", ":(literal)" + request.path}, stop);
            if (modes.success()) mode = file_content::mode_for_path(modes.stdout_str(), request.path);
            else content.error = modes.stderr_str();
        }
        if (content.error.empty()) {
            cacheKey = blob_page_key(request, content.page.blob);
            if (auto cached = cache.get(cacheKey)) {
                content.raw = std::move(cached->raw);
                content.page = std::move(cached->page);
                cacheHit = true;
                log_info("blob page cache hit for {}", content.page.blob);
            } else {
                auto result = git_run(request.repo, {"cat-file", "blob", content.page.blob}, stop, consume);
                if (!result.success() && !result.raw.outputStopped) content.error = result.stderr_str();
            }
        }
    }
    if (!request.revision.empty()) content.page.sourceIdentity = content.page.blob;
    if (!request.page.sourceIdentity.empty() && request.page.sourceIdentity != content.page.sourceIdentity)
        content.error = "File source changed; reload from the beginning";
    if (stop.stop_requested()) content.error = "File load cancelled";
    if (content.error.empty()) {
        if (!cacheHit) {
            collector.finish();
            content.error = std::move(collector.error);
            content.raw = std::move(collector.raw);
            content.page.begin = collector.begin;
            content.page.next = collector.next;
            content.page.encoding = std::move(collector.encoding);
            if (content.error.empty() && !cacheKey.empty()) cache.put(cacheKey, {content.raw, content.page});
        }
        auto decoded = file_page::decode(content.raw, content.page.encoding, content.page.begin.offset);
        content.encodingLabel = decoded.encoding;
        content.diff = parse_complete_file(request.path, decoded.text);
        content.diff.isBinary = decoded.binary;
        content.diff.isPartialContent = content.page.begin.offset != 0 || content.page.next.offset < content.page.totalBytes;
        if (!content.diff.hunks.empty()) {
            auto& hunk = content.diff.hunks.front();
            hunk.oldStart = hunk.newStart = content.page.begin.line;
            if (content.diff.isPartialContent) hunk.header = "Loaded page";
        }
        content.diff.oldMode = content.diff.newMode = mode;
        if (!decoded.binary && markdown_preview::is_markdown_path(request.path))
            content.decodedText = std::move(decoded.text);
    }
    return content;
}

async_work::Task<ecs::FullFileContent> read_file_async(FileRequest request) {
    return async_work::launch([request = std::move(request)](std::stop_token stop) {
        return read_file(request, stop);
    }, async_work::Priority::Foreground, ecs::FullFileContent{{}, {}, "Background queue is full; reopen the file to retry"});
}

}

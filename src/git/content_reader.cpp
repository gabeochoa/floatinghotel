#include "content_reader.h"
#include "blob_page_cache.h"
#include "../util/file_content.h"
#include "../util/file_page.h"
#include "../util/hunk_syntax.h"
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

ecs::FileDiff parse_complete_file(const std::string& path, const std::string& content, code_lexer::State incoming) {
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
    hunk_syntax::annotate(hunk, path, true, incoming, incoming);
    file.hunks.push_back(std::move(hunk));
    return file;
}

static ecs::FullFileContent read_file_impl(const FileRequest& input, std::stop_token stop, reading_load::Trace& trace) {
    auto run = [&](const std::vector<std::string>& args, std::function<bool(std::string_view)> consume = {}) {
        auto result = git_run(input.repo, args, stop, std::move(consume));
        trace.gitLockMs += result.lockMs;
        trace.gitProcessMs += result.processMs;
        ++trace.gitCommands;
        return result;
    };
    const auto validationStart = reading_load::Clock::now();
    FileRequest request = input;
    ecs::FullFileContent content;
    if (!request.revision.empty() && request.revision != "INDEX") {
        if (!reading::is_object_id(request.revision)) {
            auto resolved = run({"rev-parse", "--verify", "--end-of-options", request.revision + "^{commit}"});
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
    file_page::Collector collector(request.page, request.encoding, request.detectedEncoding, request.revision.empty() ? offset : 0, code_lexer::language(request.path));
    auto consume = [&](std::string_view bytes) { return !stop.stop_requested() && collector.consume(bytes); };
    if (request.revision.empty()) {
        trace.validationMs = reading_load::milliseconds(reading_load::Clock::now() - validationStart);
        reading_load::Phase reading{trace.readMs};
        auto result = file_content::read_working_file(std::filesystem::path(request.repo) / request.path, stop,
            std::numeric_limits<size_t>::max(), consume, offset);
        mode = std::move(result.mode);
        content.error = std::move(result.error);
        content.page.totalBytes = result.totalBytes;
        content.page.sourceIdentity = std::move(result.identity);
    } else {
        if (request.revision != "INDEX") {
            auto entry = run({"ls-tree", "-l", "-z", request.revision,
                "--", ":(literal)" + request.path});
            const auto& output = entry.stdout_str();
            const auto tab = output.find('\t');
            const auto end = output.find('\0');
            if (!entry.success()) content.error = entry.stderr_str();
            else if (tab == std::string::npos || end == std::string::npos ||
                     output.substr(tab + 1, end - tab - 1) != request.path)
                content.error = "File is unavailable at this revision";
            else {
                std::istringstream fields(output.substr(0, tab));
                std::string type, size;
                fields >> mode >> type >> content.page.blob >> size;
                const auto parsed = std::from_chars(size.data(), size.data() + size.size(), content.page.totalBytes);
                if (type != "blob" || !reading::is_object_id(content.page.blob) ||
                    parsed.ec != std::errc{} || parsed.ptr != size.data() + size.size())
                    content.error = type == "commit" ? "Path is a submodule; open its repository to read files" :
                        "Unable to read file metadata at this revision";
            }
        } else {
            std::string spec = request.revision == "INDEX" ? ":" + request.path
                              : request.revision + ":" + request.path;
            auto resolved = run({"rev-parse", "--verify", "--end-of-options", spec});
            if (resolved.success()) {
                content.page.blob = resolved.stdout_str();
                while (!content.page.blob.empty() && (content.page.blob.back() == '\n' || content.page.blob.back() == '\r')) content.page.blob.pop_back();
            } else content.error = resolved.stderr_str();
            if (content.error.empty()) {
                auto measured = run({"cat-file", "-s", content.page.blob});
                auto text = measured.stdout_str();
                auto parsed = std::from_chars(text.data(), text.data() + text.size(), content.page.totalBytes);
                if (!measured.success() || parsed.ec != std::errc{}) content.error = "Unable to measure file revision";
            }
            if (content.error.empty()) {
                auto modes = run(request.revision == "INDEX"
                    ? std::vector<std::string>{"ls-files", "--stage", "-z", "--", ":(literal)" + request.path}
                    : std::vector<std::string>{"ls-tree", "-z", request.revision, "--", ":(literal)" + request.path});
                if (modes.success()) mode = file_content::mode_for_path(modes.stdout_str(), request.path);
                else content.error = modes.stderr_str();
            }
        }
        if (content.error.empty()) {
            trace.validationMs = reading_load::milliseconds(reading_load::Clock::now() - validationStart);
            cacheKey = blob_page_key(request, content.page.blob);
            if (auto cached = reading_load::measure(trace.cacheMs, [&] { return cache.get(cacheKey); })) {
                content.raw = std::move(cached->raw);
                content.page = std::move(cached->page);
                cacheHit = trace.cacheHit = true;
                log_info("blob page cache hit for {}", content.page.blob);
            } else {
                reading_load::Phase reading{trace.readMs};
                auto result = run({"cat-file", "blob", content.page.blob}, consume);
                if (!result.success() && !result.raw.outputStopped) content.error = result.stderr_str();
            }
        }
    }
    if (!request.revision.empty()) content.page.sourceIdentity = content.page.blob;
    if (!request.page.sourceIdentity.empty() && request.page.sourceIdentity != content.page.sourceIdentity)
        content.error = "File source changed; reload from the beginning";
    if (stop.stop_requested()) content.error = "File load cancelled";
    if (content.error.empty()) {
        reading_load::Phase decoding{trace.decodeMs};
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
        content.diff = parse_complete_file(request.path, decoded.text, content.page.begin.lexical);
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

ecs::FullFileContent read_file(const FileRequest& request, std::stop_token stop) {
    reading_load::Trace trace;
    trace.submitted = trace.started = reading_load::Clock::now();
    auto result = read_file_impl(request, stop, trace);
    trace.finished = reading_load::Clock::now();
    result.trace = trace;
    return result;
}

async_work::Task<ecs::FullFileContent> read_file_async(FileRequest request) {
    const auto submitted = reading_load::Clock::now();
    return async_work::launch([request = std::move(request), submitted](std::stop_token stop) {
        auto result = read_file(request, stop);
        result.trace.submitted = submitted;
        return result;
    }, async_work::Priority::Foreground, ecs::FullFileContent{{}, {}, "Background queue is full; reopen the file to retry"});
}

ecs::UntrackedReviewFiles read_untracked_review_files(const std::string& repo, const std::vector<std::string>& paths,
                                                     std::stop_token stop) {
    ecs::UntrackedReviewFiles result;
    size_t remaining = 8 * 1024 * 1024;
    for (const auto& path : paths) {
        if (stop.stop_requested()) return {};
        ecs::FileDiff file;
        file.filePath = path;
        file.isNew = true;
        if (remaining >= 256 * 1024) {
            auto content = read_file({repo, path}, stop);
            if (stop.stop_requested()) return {};
            remaining -= std::min(remaining, content.raw.size());
            if (content.error.empty()) {
                file = std::move(content.diff);
                file.isNew = true;
                file.isFullContent = false;
                file.isPartialContent = content.page.next.offset < content.page.totalBytes;
                file.additions = 0;
                file.deletions = 0;
                for (auto& hunk : file.hunks) {
                    hunk.oldStart = hunk.oldCount = 0;
                    for (auto& line : hunk.lines) {
                        if (line.empty()) line = "+";
                        else line[0] = '+';
                    }
                    hunk.header = "@@ -0,0 +" + std::to_string(hunk.newStart) + "," + std::to_string(hunk.newCount) + " @@ (new file)";
                    file.additions += hunk.newCount;
                }
            } else {
                file.isPartialContent = true;
                result.notice = "Some new files could not be read: " + content.error;
            }
        } else file.isPartialContent = true;
        if (file.isPartialContent && result.notice.empty())
            result.notice = "Large new files show a bounded preview. Open source to read more.";
        result.files.push_back(std::move(file));
    }
    return result;
}

async_work::Task<ecs::UntrackedReviewFiles> read_untracked_review_files_async(std::string repo, std::vector<std::string> paths) {
    return async_work::launch([repo = std::move(repo), paths = std::move(paths)](std::stop_token stop) {
        return read_untracked_review_files(repo, paths, stop);
    }, async_work::Priority::Background, ecs::UntrackedReviewFiles{.notice = "New file previews are busy; refresh to retry."});
}

}

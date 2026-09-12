#include "../util/codeowners.h"
#include "../util/file_content.h"
#include "git_runner.h"
#include <charconv>

namespace codeowners {

async_work::Task<Document> load_async(const std::string& repo, const std::string& revision) {
    return async_work::launch([repo, revision](std::stop_token stop) {
        for (const std::string path : {".github/CODEOWNERS", "CODEOWNERS", "docs/CODEOWNERS"}) {
            if (stop.stop_requested()) return Document{{}, path, "CODEOWNERS load cancelled"};
            std::string contents;
            if (revision.empty()) {
                std::error_code error;
                auto location = std::filesystem::path(repo) / path;
                auto status = std::filesystem::symlink_status(location, error);
                if (error || !std::filesystem::is_regular_file(status)) continue;
                if (std::filesystem::file_size(location, error) > 1024 * 1024 || error)
                    return Document{{}, path, "CODEOWNERS exceeds the 1 MiB review limit"};
                auto read = file_content::read_working_file(location, stop, 1024 * 1024);
                if (!read.error.empty()) return Document{{}, path, read.error};
                contents = std::move(read.bytes);
            } else {
                auto object = git::git_run(repo, {"rev-parse", "--verify", "--end-of-options", revision == "INDEX" ? ":" + path : revision + ":" + path}, stop);
                if (!object.success()) continue;
                auto oid = object.stdout_str();
                while (!oid.empty() && (oid.back() == '\n' || oid.back() == '\r')) oid.pop_back();
                auto size = git::git_run(repo, {"cat-file", "-s", oid}, stop);
                if (!size.success()) return Document{{}, path, size.stderr_str()};
                size_t bytes = 0;
                auto value = std::from_chars(size.stdout_str().data(), size.stdout_str().data() + size.stdout_str().size(), bytes);
                if (value.ec != std::errc{} || bytes > 1024 * 1024)
                    return Document{{}, path, "CODEOWNERS exceeds the 1 MiB review limit"};
                auto result = git::git_run(repo, {"cat-file", "blob", oid}, stop);
                if (!result.success()) return Document{{}, path, result.stderr_str()};
                contents = std::move(result.raw.stdout_str);
            }
            if (contents.size() > 1024 * 1024) return Document{{}, path, "CODEOWNERS exceeds the 1 MiB review limit"};
            return parse(contents, path);
        }
        return Document{};
    }, async_work::Priority::Background, Document{{}, "CODEOWNERS", "Background queue is full; refresh to retry CODEOWNERS"});
}

}

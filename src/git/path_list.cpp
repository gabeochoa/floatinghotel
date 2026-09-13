#include "path_list.h"
#include "git_parser.h"

namespace git {

PathList read_paths(const std::string& repository, reading::SourceRevision revision,
        std::stop_token stop, size_t maxBytes) {
    PathList paths{std::move(revision)};
    if (const auto* query = std::get_if<reading::RevisionQuery>(&paths.revision)) {
        auto result = git_run(repository, {"rev-parse", "--verify", "--end-of-options", query->text + "^{commit}"}, stop);
        if (!result.success()) { paths.error = result.stderr_str(); return paths; }
        auto object = result.stdout_str();
        while (!object.empty() && (object.back() == '\n' || object.back() == '\r')) object.pop_back();
        if (!reading::is_object_id(object)) { paths.error = "Revision unavailable"; return paths; }
        paths.revision = reading::ObjectId{std::move(object)};
    }
    std::vector<std::string> args;
    if (std::holds_alternative<reading::WorkingTree>(paths.revision))
        args = {"ls-files", "--cached", "--others", "--exclude-standard", "-z"};
    else if (std::holds_alternative<reading::Index>(paths.revision)) args = {"ls-files", "--cached", "-z"};
    else args = {"ls-tree", "-r", "-z", "--name-only", reading::revision_text(paths.revision), "--"};
    std::string bytes;
    auto result = git_run(repository, args, stop, [&](std::string_view chunk) {
        const auto remaining = maxBytes - bytes.size();
        bytes.append(chunk.substr(0, remaining));
        if (chunk.size() > remaining) paths.truncated = true;
        return !paths.truncated && !stop.stop_requested();
    });
    if (stop.stop_requested()) { paths.error = "File list cancelled"; return paths; }
    if (!result.success() && !paths.truncated) {
        paths.error = result.stderr_str().empty() ? "Unable to load files for this revision" : result.stderr_str();
        return paths;
    }
    if (paths.truncated) {
        const auto end = bytes.rfind('\0');
        bytes.resize(end == std::string::npos ? 0 : end + 1);
    }
    paths.paths = parse_null_paths(bytes);
    return paths;
}

async_work::Task<PathList> load_paths_async(std::string repository, reading::SourceRevision revision) {
    auto rejected = PathList{revision, {}, "File list is busy. Try again."};
    return async_work::launch([repository = std::move(repository), revision = std::move(revision)](std::stop_token stop) {
        return read_paths(repository, revision, stop);
    }, async_work::Priority::Foreground, std::move(rejected));
}

}

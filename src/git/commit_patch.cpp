#include "commit_patch.h"
#include "git_parser.h"
#include "commit_patch_cache.h"
#include "../../vendor/afterhours/src/logging.h"
#include <filesystem>

namespace git {

ecs::CommitPatch read_commit_patch(const CommitPatchRequest& request, std::stop_token stop) {
    ecs::CommitPatch out;
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    auto resolve = [&](const std::string& revision) {
        auto result = git_run(request.repoPath, {"rev-parse", "--verify", "--end-of-options", revision + "^{commit}"}, stop);
        auto hash = result.success() ? result.stdout_str() : std::string{};
        while (!hash.empty() && (hash.back() == '\n' || hash.back() == '\r')) hash.pop_back();
        return hash;
    };
    out.resolvedCommit = resolve(request.commit);
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    if (out.resolvedCommit.empty()) { out.error = "Unable to resolve commit"; return out; }
    out.resolvedParent = resolve(request.parent.empty() ? out.resolvedCommit + "^" : request.parent);
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    if (!request.parent.empty() && out.resolvedParent.empty()) { out.error = "Unable to resolve selected parent"; return out; }
    std::error_code identityError;
    auto repository = std::filesystem::canonical(request.repoPath, identityError);
    auto common = git_run(request.repoPath, {"rev-parse", "--path-format=absolute", "--git-common-dir"}, stop);
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    auto commonDirectory = common.stdout_str();
    while (!commonDirectory.empty() && (commonDirectory.back() == '\n' || commonDirectory.back() == '\r')) commonDirectory.pop_back();
    CommitPatchKey key{repository.string(), commonDirectory, out.resolvedCommit, out.resolvedParent, request.context, request.ignoreWhitespace};
    bool cacheable = !identityError && common.success() && !commonDirectory.empty();
    static CommitPatchCache cache;
    if (cacheable) {
        if (auto cached = cache.get(key)) {
            log_info("commit patch cache hit for {}", out.resolvedCommit);
            return std::move(*cached);
        }
    }
    std::vector<std::string> args;
    if (request.parent.empty()) args = {"show", "--first-parent", "--format=", out.resolvedCommit};
    else args = {"diff", out.resolvedParent, out.resolvedCommit};
    args.push_back("--unified=" + std::to_string(request.context));
    if (request.ignoreWhitespace) args.push_back("--ignore-all-space");
    args.push_back("--");
    auto result = git_run(request.repoPath, args, stop);
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    if (!result.success()) { out.error = "Unable to load commit diff: " + result.stderr_str(); return out; }
    out.files = parse_diff(result.stdout_str());
    if (stop.stop_requested()) { out.files.clear(); out.error = "Commit patch request cancelled"; }
    else if (cacheable) cache.put(key, out);
    return out;
}

async_work::Task<ecs::CommitPatch> load_commit_patch_async(CommitPatchRequest request) {
    return async_work::launch([request = std::move(request)](std::stop_token stop) {
        return read_commit_patch(request, stop);
    }, async_work::Priority::Foreground, ecs::CommitPatch{.error = "Background queue is full; retry the commit"});
}

}

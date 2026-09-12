#include "commit_patch.h"
#include "git_parser.h"
#include "commit_patch_cache.h"
#include "../../vendor/afterhours/src/logging.h"
#include <filesystem>
#include <sstream>

namespace git {

ecs::CommitPatch read_commit_patch(const CommitPatchRequest& request, std::stop_token stop) {
    ecs::CommitPatch out;
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    auto resolved = git_run(request.repoPath, {"rev-parse", "--path-format=absolute", "--git-common-dir",
        "--revs-only", "--end-of-options", request.commit + "^{commit}",
        (request.parent.empty() ? request.commit + "^1" : request.parent) + "^{commit}"}, stop);
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    std::istringstream identities(resolved.stdout_str());
    std::string commonDirectory;
    std::getline(identities, commonDirectory);
    std::getline(identities, out.resolvedCommit);
    std::getline(identities, out.resolvedParent);
    if (!resolved.success() || !reading::is_object_id(out.resolvedCommit)) {
        out.error = "Unable to resolve commit"; return out;
    }
    if (!request.parent.empty() && !reading::is_object_id(out.resolvedParent)) {
        out.error = "Unable to resolve selected parent"; return out;
    }
    std::error_code identityError;
    auto repository = std::filesystem::canonical(request.repoPath, identityError);
    CommitPatchKey key{repository.string(), commonDirectory, out.resolvedCommit,
        out.resolvedParent, request.context, request.ignoreWhitespace, request.parent.empty()};
    bool cacheable = !identityError && !commonDirectory.empty();
    auto& cache = commit_patch_cache();
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
    auto metadata = git_run(request.repoPath, {"show", out.resolvedCommit, "--no-patch",
        "--format=%s%x00%b%x00%an%x00%ae%x00%aI%x00%P"}, stop);
    if (stop.stop_requested()) { out.files.clear(); out.error = "Commit patch request cancelled"; return out; }
    if (!metadata.success()) { out.error = "Unable to load commit metadata: " + metadata.stderr_str(); return out; }
    out.metadata = metadata.stdout_str();
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

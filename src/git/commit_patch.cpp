#include "commit_patch.h"
#include "git_parser.h"
#include "commit_patch_cache.h"
#include "../util/lfs_pointer.h"
#include "../util/shared_read.h"
#include <afterhours/src/logging.h>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace git {

static auto& pending_patch_reads() {
    static async_work::SharedRead<CommitPatchKey, ecs::CommitPatch> reads;
    return reads;
}

std::uint64_t commit_patch_shared_reads() { return pending_patch_reads().shared_count(); }

namespace {
struct Resolution { std::string repository, commonDirectory, parent; };
std::mutex resolutionMutex;
std::unordered_map<std::string, Resolution> resolutions;  // ponytail: unbounded, one small entry per commit viewed

std::string resolution_key(const CommitPatchRequest& request) {
    return request.repoPath + '\n' + request.commit + '\n' + request.parent;
}
}

std::optional<ecs::CommitPatch> peek_commit_patch(const CommitPatchRequest& request) {
    if (!reading::is_object_id(request.commit) || (!request.parent.empty() && !reading::is_object_id(request.parent))) return {};
    Resolution resolved;
    {
        std::lock_guard lock(resolutionMutex);
        auto found = resolutions.find(resolution_key(request));
        if (found == resolutions.end()) return {};
        resolved = found->second;
    }
    return commit_patch_cache().get({resolved.repository, resolved.commonDirectory, request.commit, resolved.parent,
        request.context, request.ignoreWhitespace, request.parent.empty()});
}


// One-spawn load for the common case (first-parent patch of a full commit
// id in a non-shallow repo): a single `git show` whose format carries the
// resolved id, parents and metadata ahead of the diff, split at a NUL
// sentinel. This used to take three sequential spawns (rev-parse, show,
// show --no-patch), and on machines where a spawn costs hundreds of ms a
// tiny commit took seconds to open. Explicit-parent and shallow requests
// keep the rev-parse path below: a shallow boundary's first parent is a
// hash whose object may not exist, and only rev-parse reports that.
static std::optional<ecs::CommitPatch> read_commit_patch_fast(
        const CommitPatchRequest& request, std::stop_token stop, reading_load::Trace& trace,
        const std::string& repository, const std::filesystem::path& commonDirectory) {
    CommitPatchKey sharedKey{repository, commonDirectory.string(), request.commit, {},
        request.context, request.ignoreWhitespace, true};
    auto& reads = pending_patch_reads();
    auto load = [&](std::stop_token sharedStop) {
        ecs::CommitPatch out;
        std::vector<std::string> args{"show", "--first-parent",
            "--format=%H%x00%s%x00%b%x00%an%x00%ae%x00%aI%x00%P%x00FH_PATCH_END", request.commit};
        args.push_back("--unified=" + std::to_string(request.context));
        if (request.ignoreWhitespace) args.push_back("--ignore-all-space");
        args.push_back("--");
        auto result = reading_load::measure(trace.readMs, [&] { return git_run(request.repoPath, args, sharedStop); });
        trace.gitCommands = 1;
        trace.gitLockMs = result.lockMs;
        trace.gitProcessMs = result.processMs;
        if (sharedStop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
        if (!result.success()) { out.error = "Unable to resolve commit"; return out; }
        const std::string sentinel("\0FH_PATCH_END", 13);
        const auto& output = result.stdout_str();
        const auto boundary = output.find(sentinel);
        if (boundary == std::string::npos) { out.error = "Unable to resolve commit"; return out; }
        const std::string head = output.substr(0, boundary);
        std::vector<std::string> fields;
        for (size_t start = 0;;) {
            const auto nul = head.find('\0', start);
            if (nul == std::string::npos) { fields.push_back(head.substr(start)); break; }
            fields.push_back(head.substr(start, nul - start));
            start = nul + 1;
        }
        if (fields.size() < 7 || !reading::is_object_id(fields[0])) { out.error = "Unable to resolve commit"; return out; }
        out.resolvedCommit = fields[0];
        const auto& parents = fields[6];
        out.resolvedParent = parents.substr(0, parents.find(' '));
        if (out.resolvedParent.size() == parents.size() && parents.empty()) out.resolvedParent.clear();
        out.metadata = head.substr(fields[0].size() + 1);
        CommitPatchKey key{repository, commonDirectory.string(), out.resolvedCommit,
            out.resolvedParent, request.context, request.ignoreWhitespace, true};
        {
            std::lock_guard lock(resolutionMutex);
            resolutions[resolution_key(request)] = {key.repository, key.commonDirectory, key.parent};
        }
        auto& cache = commit_patch_cache();
        if (auto cached = reading_load::measure(trace.cacheMs, [&] { return cache.get(key); })) {
            trace.cacheHit = true;
            return std::move(*cached);
        }
        std::string diffText = output.substr(boundary + sentinel.size());
        const auto first = diffText.find_first_not_of("\r\n");
        diffText = first == std::string::npos ? std::string{} : diffText.substr(first);
        out.files = reading_load::measure(trace.decodeMs, [&] { return parse_diff(diffText); });
        if (sharedStop.stop_requested()) { out.files.clear(); out.error = "Commit patch request cancelled"; return out; }
        cache.put(key, out);
        return out;
    };
    const auto started = reading_load::Clock::now();
    auto result = reads.run(sharedKey, stop, ecs::CommitPatch{.error = "Commit patch request cancelled"}, load);
    trace.sharedWaitMs = std::max(0., reading_load::milliseconds(reading_load::Clock::now() - started) - trace.readMs - trace.decodeMs);
    return result;
}

static ecs::CommitPatch read_commit_patch_impl(const CommitPatchRequest& request, std::stop_token stop, reading_load::Trace& trace) {
    const auto validationStart = reading_load::Clock::now();
    ecs::CommitPatch out;
    if (stop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
    if (request.parent.empty() && reading::is_object_id(request.commit)) {
        std::error_code identityError;
        const auto repository = std::filesystem::canonical(request.repoPath, identityError);
        auto commonDirectory = identityError ? std::filesystem::path{} : lfs_pointer::common_git_directory(repository);
        if (!commonDirectory.empty()) {
            std::error_code canonError;
            commonDirectory = std::filesystem::weakly_canonical(commonDirectory, canonError);
            if (canonError) commonDirectory.clear();
        }
        std::error_code shallowError;
        const bool shallow = !commonDirectory.empty() && std::filesystem::exists(commonDirectory / "shallow", shallowError);
        if (!commonDirectory.empty() && !shallow)
            if (auto fast = read_commit_patch_fast(request, stop, trace, repository.string(), commonDirectory))
                return std::move(*fast);
    }
    auto resolved = git_run(request.repoPath, {"rev-parse", "--path-format=absolute", "--git-common-dir",
        "--revs-only", "--end-of-options", request.commit + "^{commit}",
        (request.parent.empty() ? request.commit + "^1" : request.parent) + "^{commit}"}, stop);
    trace.gitCommands = 1;
    trace.gitLockMs = resolved.lockMs;
    trace.gitProcessMs = resolved.processMs;
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
    if (cacheable && reading::is_object_id(request.commit)) {
        std::lock_guard lock(resolutionMutex);
        resolutions[resolution_key(request)] = {key.repository, key.commonDirectory, key.parent};
    }
    trace.validationMs = reading_load::milliseconds(reading_load::Clock::now() - validationStart);
    auto& cache = commit_patch_cache();
    if (cacheable) {
        if (auto cached = reading_load::measure(trace.cacheMs, [&] { return cache.get(key); })) {
            trace.cacheHit = true;
            log_info("commit patch cache hit for {}", out.resolvedCommit);
            return std::move(*cached);
        }
    }
    auto& reads = pending_patch_reads();
    auto load = [&](std::stop_token sharedStop) {
        if (cacheable) if (auto cached = reading_load::measure(trace.cacheMs, [&] { return cache.get(key); })) { trace.cacheHit = true; return std::move(*cached); }
        std::vector<std::string> args;
        if (request.parent.empty()) args = {"show", "--first-parent", "--format=", out.resolvedCommit};
        else args = {"diff", out.resolvedParent, out.resolvedCommit};
        args.push_back("--unified=" + std::to_string(request.context));
        if (request.ignoreWhitespace) args.push_back("--ignore-all-space");
        args.push_back("--");
        auto result = reading_load::measure(trace.readMs, [&] { return git_run(request.repoPath, args, sharedStop); });
        ++trace.gitCommands; trace.gitLockMs += result.lockMs; trace.gitProcessMs += result.processMs;
        if (sharedStop.stop_requested()) { out.error = "Commit patch request cancelled"; return out; }
        if (!result.success()) { out.error = "Unable to load commit diff: " + result.stderr_str(); return out; }
        out.files = reading_load::measure(trace.decodeMs, [&] { return parse_diff(result.stdout_str()); });
        auto metadata = reading_load::measure(trace.readMs, [&] { return git_run(request.repoPath, {"show", out.resolvedCommit, "--no-patch",
            "--format=%s%x00%b%x00%an%x00%ae%x00%aI%x00%P"}, sharedStop); });
        ++trace.gitCommands; trace.gitLockMs += metadata.lockMs; trace.gitProcessMs += metadata.processMs;
        if (sharedStop.stop_requested()) { out.files.clear(); out.error = "Commit patch request cancelled"; return out; }
        if (!metadata.success()) { out.error = "Unable to load commit metadata: " + metadata.stderr_str(); return out; }
        out.metadata = metadata.stdout_str();
        if (sharedStop.stop_requested()) { out.files.clear(); out.error = "Commit patch request cancelled"; }
        else if (cacheable) cache.put(key, out);
        return out;
    };
    if (!cacheable) return load(stop);
    const auto started = reading_load::Clock::now();
    auto result = reads.run(key, stop, ecs::CommitPatch{.error = "Commit patch request cancelled"}, load);
    trace.sharedWaitMs = std::max(0., reading_load::milliseconds(reading_load::Clock::now() - started) - trace.readMs - trace.decodeMs);
    return result;
}

ecs::CommitPatch read_commit_patch(const CommitPatchRequest& request, std::stop_token stop) {
    reading_load::Trace trace;
    trace.submitted = trace.started = reading_load::Clock::now();
    auto result = read_commit_patch_impl(request, stop, trace);
    trace.finished = reading_load::Clock::now();
    result.trace = trace;
    return result;
}

async_work::Task<ecs::CommitPatch> load_commit_patch_async(CommitPatchRequest request) {
    const auto submitted = reading_load::Clock::now();
    return async_work::launch([request = std::move(request), submitted](std::stop_token stop) {
        auto result = read_commit_patch(request, stop);
        result.trace.submitted = submitted;
        return result;
    }, async_work::Priority::Foreground, ecs::CommitPatch{.error = "Background queue is full; retry the commit"});
}

std::optional<async_work::Task<bool>> prefetch_commit_patch_async(CommitPatchRequest request) {
    if (!reading::is_object_id(request.commit) || (!request.parent.empty() && !reading::is_object_id(request.parent))) return {};
    struct Permit {};
    static std::mutex mutex;
    static std::weak_ptr<Permit> outstanding;
    std::shared_ptr<Permit> permit;
    {
        std::lock_guard lock(mutex);
        if (!outstanding.expired()) return {};
        permit = std::make_shared<Permit>();
        outstanding = permit;
    }
    return async_work::launch([request = std::move(request), permit = std::move(permit)](std::stop_token stop) {
        (void)permit;
        return read_commit_patch(request, stop).error.empty();
    }, async_work::Priority::Background, false);
}

}

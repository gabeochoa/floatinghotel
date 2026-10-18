#pragma once

#include "../ecs/components.h"

namespace git {

std::uint64_t commit_patch_shared_reads();

struct CommitPatchRequest {
    std::string repoPath;
    std::string commit;
    std::string parent;
    int context = 3;
    bool ignoreWhitespace = false;
};

ecs::CommitPatch read_commit_patch(const CommitPatchRequest& request, std::stop_token stop = {});
// The cached patch for a full commit id, using the repository directory and
// parent its last validated read resolved to; no git process. A caller shows
// it at once and still runs read_commit_patch, whose answer (including a
// missing object or a deepened shallow parent) replaces it.
std::optional<ecs::CommitPatch> peek_commit_patch(const CommitPatchRequest& request);
async_work::Task<ecs::CommitPatch> load_commit_patch_async(CommitPatchRequest request);
std::optional<async_work::Task<bool>> prefetch_commit_patch_async(CommitPatchRequest request);

}

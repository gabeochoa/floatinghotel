#pragma once

#include "../ecs/components.h"

namespace git {

struct CommitPatchRequest {
    std::string repoPath;
    std::string commit;
    std::string parent;
    int context = 3;
    bool ignoreWhitespace = false;
};

ecs::CommitPatch read_commit_patch(const CommitPatchRequest& request, std::stop_token stop = {});
async_work::Task<ecs::CommitPatch> load_commit_patch_async(CommitPatchRequest request);

}

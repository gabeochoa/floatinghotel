#pragma once

#include <chrono>
#include <utility>

#include <afterhours/src/logging.h>

#include "components.h"
#include "query_helpers.h"
#include "../git/git_runner.h"

namespace ecs {

inline bool toast_on_git_failure(const git::GitResult& result,
                                  const std::string& action) {
    if (result.success()) return false;
    std::string msg = action + " failed";
    auto& err = result.stderr_str();
    if (!err.empty()) {
        auto nl = err.find('\n');
        auto firstLine = err.substr(0, nl);
        if (!firstLine.empty()) msg += ": " + firstLine;
    }
    auto* menu = find_singleton<MenuComponent>();
    if (menu) menu->pendingToasts.push_back({msg, MenuComponent::Notice::Kind::Error});
    return true;
}

// Enqueue a network git operation to run on a background thread.
// The NetworkOpsPollingSystem will poll the future, show a toast on
// completion/failure, and trigger a repo refresh.
inline void enqueue_network_op(const std::string& label,
                               async_work::Task<git::GitResult> fut) {
    auto* ops = find_singleton<NetworkOpsComponent>();
    if (!ops) return;

    auto* ent = find_singleton_entity<RepoComponent, ActiveTab>();
    afterhours::EntityID tabId = ent ? ent->id : 0;

    PendingNetworkOp op;
    op.label = label;
    op.slot.request(label, std::move(fut));
    op.tabId = tabId;
    op.repository = ent ? ent->get<RepoComponent>().repoPath : "";
    op.startedAt = std::chrono::steady_clock::now();
    ops->pending.push_back(std::move(op));
}

struct NetworkOpsPollingSystem : afterhours::System<NetworkOpsComponent> {
    void for_each_with(afterhours::Entity&, NetworkOpsComponent& ops,
                       float) override {
        for (auto it = ops.pending.begin(); it != ops.pending.end(); ) {
            auto result = it->slot.poll_unstamped();
            const bool failed = !result.has_value() &&
                it->slot.phase == async_slot::Phase::Failed;
            if (!result.has_value() && !failed) { ++it; continue; }

            std::string label = it->label;
            afterhours::EntityID tabId = it->tabId;

            std::string toastMsg;
            MenuComponent::Notice::Kind toastKind =
                MenuComponent::Notice::Kind::Error;
            if (failed) {
                toastMsg = label + " failed";
                if (!it->slot.error.empty()) toastMsg += ": " + it->slot.error;
            } else if (result->raw.cancelled) {
                toastMsg = label + " cancelled";
            } else if (result->success()) {
                toastMsg = label + " succeeded";
                toastKind = MenuComponent::Notice::Kind::Success;
            } else {
                toastMsg = label + " failed";
                auto& err = result->stderr_str();
                if (!err.empty()) {
                    auto firstLine = err.substr(0, err.find('\n'));
                    if (!firstLine.empty()) toastMsg += ": " + firstLine;
                }
            }

            auto* menu = find_singleton<MenuComponent>();
            if (menu) menu->pendingToasts.push_back({toastMsg, toastKind});

            auto opt = afterhours::EntityHelper::getEntityForID(tabId);
            if (opt.valid() && !opt->cleanup && opt->has<RepoComponent>() && opt->get<RepoComponent>().repoPath == it->repository) {
                opt->get<RepoComponent>().request_refresh();
            }

            it = ops.pending.erase(it);
        }
    }
};

}  // namespace ecs

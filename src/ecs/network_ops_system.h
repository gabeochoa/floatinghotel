#pragma once

#include <chrono>

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

    ops->pending.push_back({label, std::move(fut), tabId});
}

struct NetworkOpsPollingSystem : afterhours::System<NetworkOpsComponent> {
    void for_each_with(afterhours::Entity&, NetworkOpsComponent& ops,
                       float) override {
        using namespace std::chrono_literals;

        for (auto it = ops.pending.begin(); it != ops.pending.end(); ) {
            if (it->future.wait_for(0s) == std::future_status::ready) {
                auto result = it->future.get();
                std::string label = it->label;
                afterhours::EntityID tabId = it->tabId;

                std::string toastMsg;
                if (result.success()) {
                    toastMsg = label + " succeeded";
                } else {
                    toastMsg = label + " failed";
                    auto& err = result.stderr_str();
                    if (!err.empty()) {
                        auto firstLine = err.substr(0, err.find('\n'));
                        if (!firstLine.empty()) {
                            toastMsg += ": " + firstLine;
                        }
                    }
                }

                auto* menu = find_singleton<MenuComponent>();
                if (menu) menu->pendingToasts.push_back({toastMsg, result.success()
                    ? MenuComponent::Notice::Kind::Success : MenuComponent::Notice::Kind::Error});

                auto opt = afterhours::EntityHelper::getEntityForID(tabId);
                if (opt.valid() && opt->has<RepoComponent>()) {
                    opt->get<RepoComponent>().refreshRequested = true;
                }

                it = ops.pending.erase(it);
            } else {
                ++it;
            }
        }
    }
};

}  // namespace ecs

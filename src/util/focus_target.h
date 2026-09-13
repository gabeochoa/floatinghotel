#pragma once

#include "reading_workspace.h"

namespace reading::focus {

enum class Region { Tree, History, DocumentTabs, Code, Picker, Find, Search, SearchPreview, Feedback, Menu };
enum class Popup { Menu, ContextMenu, Picker, Find, Search, SearchPreview, Feedback, CommitSearch, FileHistory };

struct Target {
    std::string repository;
    DocumentId document;
    Region region = Region::Code;
    std::string item;
    std::string control;
    bool operator==(const Target&) const = default;
};

struct ReturnPoint {
    Popup popup;
    std::optional<Target> target;
    std::uint64_t generation;
};

struct State {
    std::string repository;
    std::optional<Target> origin;
    std::optional<Target> pending;
    std::uint64_t pendingGeneration = 0;
    std::vector<ReturnPoint> returns;

    void sync(const std::string& repo, std::uint64_t generation, const std::vector<Popup>& visible) {
        if (repository != repo) {
            repository = repo;
            returns.clear();
            pending.reset();
        }
        if (pending && pendingGeneration != generation) pending.reset();
        for (size_t i = returns.size(); i > 0; --i) {
            const auto& point = returns[i - 1];
            if (std::find(visible.begin(), visible.end(), point.popup) != visible.end()) continue;
            pending = point.generation == generation ? point.target : std::nullopt;
            pendingGeneration = generation;
            returns.erase(returns.begin() + static_cast<std::ptrdiff_t>(i - 1));
        }
        for (const auto popup : visible) {
            if (std::none_of(returns.begin(), returns.end(), [&](const auto& point) { return point.popup == popup; })) {
                returns.push_back({popup, origin && origin->repository == repo ? origin : std::nullopt, generation});
                pending.reset();
            }
        }
        if (pending && pending->repository != repo) pending.reset();
    }

    bool valid(const Target& target, const ReadingWorkspace& workspace) const {
        if (target.repository != repository) return false;
        if (target.document.value == 0) return true;
        if (!workspace.document(target.document)) return false;
        return target.region == Region::DocumentTabs || target.document == workspace.active_id();
    }
};

}

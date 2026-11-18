#pragma once

#include "reading_workspace.h"

namespace reading::focus {

enum class Region { Tree, History, DocumentTabs, Code, Picker, Find, Search, SearchPreview, Feedback, Menu };
enum class Popup { Menu, ContextMenu, Picker, Find, Search, SearchPreview, Feedback, CommitSearch, FileHistory, Composer, Options, Snapshot, ComparisonEditor, CommitDetails, PushDialog, RelinkDialog, Shortcuts, NewBranch, DeleteBranch, ForceDeleteBranch, Stash };

inline std::string_view popup_name(Popup popup) {
    switch (popup) {
        case Popup::Menu: return "Menu";
        case Popup::ContextMenu: return "ContextMenu";
        case Popup::Picker: return "Picker";
        case Popup::Find: return "Find";
        case Popup::Search: return "Search";
        case Popup::SearchPreview: return "SearchPreview";
        case Popup::Feedback: return "Feedback";
        case Popup::CommitSearch: return "CommitSearch";
        case Popup::FileHistory: return "FileHistory";
        case Popup::Composer: return "Composer";
        case Popup::Options: return "Options";
        case Popup::Snapshot: return "Snapshot";
        case Popup::ComparisonEditor: return "ComparisonEditor";
        case Popup::CommitDetails: return "CommitDetails";
        case Popup::PushDialog: return "PushDialog";
        case Popup::RelinkDialog: return "RelinkDialog";
        case Popup::Shortcuts: return "Shortcuts";
        case Popup::NewBranch: return "NewBranch";
        case Popup::DeleteBranch: return "DeleteBranch";
        case Popup::ForceDeleteBranch: return "ForceDeleteBranch";
        case Popup::Stash: return "Stash";
    }
    return "Menu";
}

struct Target {
    std::string repository;
    DocumentId document;
    Region region = Region::Code;
    std::string item;
    std::string control;
    bool operator==(const Target&) const = default;
};

struct ShortcutOwner {
    std::optional<Region> region;
    bool text = false;

    bool reader() const {
        return !text && (region == Region::Code || region == Region::DocumentTabs);
    }
    bool input(Region expected) const { return text && region == expected; }
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

    // Named view of the lifecycle (fsm_todo.md M27): Idle while reading,
    // PopupOpen while any popup holds a return point, ReturnPending while
    // a closed popup's focus return waits to be consumed. Computed, so it
    // cannot disagree with the fields it names.
    enum class Phase { Idle, PopupOpen, ReturnPending };
    Phase phase() const {
        if (pending) return Phase::ReturnPending;
        if (!returns.empty()) return Phase::PopupOpen;
        return Phase::Idle;
    }

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
                // A popup that replaces one closing this frame (menu item -> dialog)
                // returns to where the closing one would have.
                const auto from = pending ? pending : origin;
                returns.push_back({popup, from && from->repository == repo ? from : std::nullopt, generation});
                pending.reset();
            }
        }
        if (pending && pending->repository != repo) pending.reset();
    }

    std::optional<Popup> topmost() const {
        for (auto it = returns.rbegin(); it != returns.rend(); ++it)
            if (it->popup == Popup::ContextMenu || it->popup == Popup::Menu) return it->popup;
        return returns.empty() ? std::nullopt : std::optional{returns.back().popup};
    }

    bool valid(const Target& target, const ReadingWorkspace& workspace) const {
        if (target.repository != repository) return false;
        if (target.document.value == 0) return true;
        if (!workspace.document(target.document)) return false;
        return target.region == Region::DocumentTabs || target.document == workspace.active_id();
    }
};

inline std::string_view phase_name(State::Phase phase) {
    switch (phase) {
        case State::Phase::Idle: return "Idle";
        case State::Phase::PopupOpen: return "PopupOpen";
        case State::Phase::ReturnPending: return "ReturnPending";
    }
    return "Idle";
}

}

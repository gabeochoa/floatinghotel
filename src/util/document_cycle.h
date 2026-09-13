#pragma once

#include "reading_workspace.h"

namespace reading {

inline std::vector<DocumentId> recent_documents(const ReadingWorkspace& workspace) {
    std::vector<const Document*> documents;
    for (const auto& document : workspace.documents()) documents.push_back(&document);
    std::stable_sort(documents.begin(), documents.end(), [&](const auto* a, const auto* b) {
        if (a->id == workspace.active_id()) return b->id != workspace.active_id();
        if (b->id == workspace.active_id()) return false;
        return a->lastActivated > b->lastActivated;
    });
    std::vector<DocumentId> ids;
    for (const auto* document : documents) ids.push_back(document->id);
    return ids;
}

struct DocumentCycle {
    std::vector<DocumentId> order;
    size_t index = 0;

    std::optional<DocumentId> selected() const {
        return order.empty() ? std::nullopt : std::optional{order[index]};
    }
    void advance(int direction) {
        if (order.empty() || direction == 0) return;
        index = direction < 0 ? (index + order.size() - 1) % order.size() : (index + 1) % order.size();
    }
    void retain(const std::vector<DocumentId>& open) {
        const auto previous = selected();
        std::erase_if(order, [&](auto id) { return std::find(open.begin(), open.end(), id) == open.end(); });
        if (order.empty()) { index = 0; return; }
        const auto found = std::find(order.begin(), order.end(), previous.value());
        index = found != order.end() ? static_cast<size_t>(found - order.begin()) : std::min(index, order.size() - 1);
    }
};

}

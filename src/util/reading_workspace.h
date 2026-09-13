#pragma once

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <optional>
#include <string>
#include <variant>
#include <type_traits>
#include <utility>
#include <vector>

#include "diff_revisions.h"

struct navigation;

namespace reading {

struct WorkingTree { bool operator==(const WorkingTree&) const = default; };
struct Index { bool operator==(const Index&) const = default; };
struct RevisionQuery {
    std::string text;
    bool operator==(const RevisionQuery&) const = default;
};
struct ObjectId {
    std::string value;
    bool operator==(const ObjectId&) const = default;
};
using HistoricalRevision = std::variant<RevisionQuery, ObjectId>;
using SourceRevision = std::variant<WorkingTree, Index, RevisionQuery, ObjectId>;

inline bool is_object_id(const std::string& text) {
    return (text.size() == 40 || text.size() == 64) &&
        std::all_of(text.begin(), text.end(), [](char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        });
}

inline HistoricalRevision historical_revision(std::string text) {
    if (is_object_id(text)) return ObjectId{std::move(text)};
    return RevisionQuery{std::move(text)};
}

inline SourceRevision source_revision(std::string text) {
    if (text.empty()) return WorkingTree{};
    if (text == "INDEX") return Index{};
    if (is_object_id(text)) return ObjectId{std::move(text)};
    return RevisionQuery{std::move(text)};
}

template<class... T>
inline const std::string& revision_text(const std::variant<T...>& revision) {
    return std::visit([](const auto& value) -> const std::string& {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, WorkingTree>) { static const std::string empty; return empty; }
        else if constexpr (std::is_same_v<V, Index>) { static const std::string index = "INDEX"; return index; }
        else if constexpr (std::is_same_v<V, ObjectId>) return value.value;
        else return value.text;
    }, revision);
}

struct WorkingChanges {
    bool staged = false;
    bool operator==(const WorkingChanges&) const = default;
};
struct CommitReview {
    HistoricalRevision commit;
    std::optional<HistoricalRevision> parent;
    bool operator==(const CommitReview&) const = default;
};
struct ComparisonReview {
    HistoricalRevision before;
    HistoricalRevision after;
    bool operator==(const ComparisonReview&) const = default;
};
using ReviewDestination = std::variant<WorkingChanges, CommitReview, ComparisonReview>;

enum class DiffSide { Before, After };

struct ReadingAnchor {
    std::string path;
    std::string revision;
    DiffSide side = DiffSide::After;
    int line = 1;
    int column = 1;
    float viewportFraction = 0.f;
    char sign = ' ';
    bool operator==(const ReadingAnchor&) const = default;
};

struct ReviewLocation {
    ReviewDestination destination = WorkingChanges{};
    std::string file;
    bool operator==(const ReviewLocation&) const = default;
};
struct SourceDestination {
    std::string path;
    SourceRevision revision;
    bool operator==(const SourceDestination&) const = default;
};
struct SourceLocation {
    SourceDestination destination;
    int line = 0;
    std::optional<ReviewLocation> origin;
    int column = 1;
    std::optional<ReadingAnchor> originAnchor;
    bool operator==(const SourceLocation&) const = default;
};
using Location = std::variant<ReviewLocation, SourceLocation>;

inline SourceRevision source_revision_for(const Location& location) {
    return std::visit([](const auto& target) -> SourceRevision {
        using T = std::decay_t<decltype(target)>;
        if constexpr (std::is_same_v<T, SourceLocation>) return target.destination.revision;
        else return std::visit([](const auto& review) -> SourceRevision {
            using R = std::decay_t<decltype(review)>;
            if constexpr (std::is_same_v<R, WorkingChanges>) {
                if (review.staged) return Index{};
                return WorkingTree{};
            } else if constexpr (std::is_same_v<R, CommitReview>)
                return std::visit([](const auto& revision) -> SourceRevision { return revision; }, review.commit);
            else return std::visit([](const auto& revision) -> SourceRevision { return revision; }, review.after);
        }, target.destination);
    }, location);
}


inline ReviewLocation review(const std::string& scope, std::string file = {}) {
    const auto target = diff_target(scope);
    switch (target.kind) {
        case DiffTarget::Kind::WorkingTree: return {WorkingChanges{}, std::move(file)};
        case DiffTarget::Kind::Index: return {WorkingChanges{true}, std::move(file)};
        case DiffTarget::Kind::Comparison:
            return {ComparisonReview{historical_revision(target.before), historical_revision(target.after)}, std::move(file)};
        case DiffTarget::Kind::ParentComparison:
            return {CommitReview{historical_revision(target.after), historical_revision(target.before)}, std::move(file)};
        case DiffTarget::Kind::File:
        case DiffTarget::Kind::Commit:
            return {CommitReview{historical_revision(target.after), {}}, std::move(file)};
    }
    return {};
}

inline SourceLocation source(std::string path, std::string revision = {}, int line = 0,
                             std::optional<ReviewLocation> origin = {}) {
    return {{std::move(path), source_revision(std::move(revision))}, line, std::move(origin)};
}

inline std::string scope(const ReviewLocation& location) {
    return std::visit([](const auto& destination) -> std::string {
        using T = std::decay_t<decltype(destination)>;
        if constexpr (std::is_same_v<T, WorkingChanges>) return destination.staged ? "index" : "wt";
        else if constexpr (std::is_same_v<T, CommitReview>) {
            auto commit = revision_text(destination.commit);
            return destination.parent ? "parent:" + revision_text(*destination.parent) + ":" + commit : commit;
        } else return "compare:" + revision_text(destination.before) + ":" + revision_text(destination.after);
    }, location.destination);
}

enum class Slot { Review, Source };
enum class OpenMode { Preview, Keep };
enum class ClickRegion { Tabs, Tree, History, Picker, Search };

struct Visit {
    Location location = ReviewLocation{};
    bool reviewing = false;
    std::optional<ReadingAnchor> anchor;
    bool operator==(const Visit&) const = default;
};

struct DocumentId {
    std::uint64_t value = 0;
    bool operator==(const DocumentId&) const = default;
};

inline bool same_document(const Location& a, const Location& b) {
    if (a.index() != b.index()) return false;
    if (const auto* review = std::get_if<ReviewLocation>(&a))
        return review->destination == std::get<ReviewLocation>(b).destination;
    return std::get<SourceLocation>(a).destination == std::get<SourceLocation>(b).destination;
}

struct FileSummary {
    std::string path;
    int additions = 0;
    int deletions = 0;
    std::string oldPath;
    char change = 'M';
    std::string signature;
    std::vector<std::string> hunkKeys;
    bool requiresFileRecord = false;
    bool partial = false;
};

struct Document {
    DocumentId id;
    Location location = ReviewLocation{};
    std::uint64_t lastActivated = 0;
    std::optional<std::vector<FileSummary>> files;
    bool preview = false;
    std::string subject;
    std::optional<ReadingAnchor> anchor;
    bool restoreAnchor = false;
    bool unresolvedSavedRevision = false;
};

class ReadingWorkspace {
    friend struct ::navigation;
    std::vector<Document> documents_{{DocumentId{1}}};
    std::vector<Document> closed_;
    DocumentId active_{1};
    std::uint64_t nextId_ = 2;
    std::vector<Visit> history_{{}};
    size_t index_ = 0;
    std::uint64_t generation_ = 0;
    std::optional<Location> lastClick_;
    ClickRegion lastClickRegion_ = ClickRegion::Tree;
    std::chrono::steady_clock::time_point lastClickTime_;

    Document& current() {
        return *std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) { return tab.id == active_; });
    }
    void keep(DocumentId id) {
        for (auto& document : documents_) if (document.id == id) document.preview = false;
    }
    void select(const Location& location, OpenMode mode = OpenMode::Preview) {
        if (const auto* source = std::get_if<SourceLocation>(&location); source && source->origin)
            if (const auto* origin = document(*source->origin)) keep(origin->id);
        auto found = std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) {
            return same_document(tab.location, location);
        });
        if (found == documents_.end()) {
            if (mode == OpenMode::Preview)
                found = std::find_if(documents_.begin(), documents_.end(), [](const auto& tab) { return tab.preview; });
            Document next{DocumentId{nextId_++}, location, 0, {}, mode == OpenMode::Preview};
            if (found == documents_.end()) {
                documents_.push_back(std::move(next));
                found = std::prev(documents_.end());
            } else *found = std::move(next);
        }
        if (mode == OpenMode::Keep) found->preview = false;
        found->location = location;
        active_ = found->id;
        found->lastActivated = ++generation_;
    }
public:
    const std::vector<Document>& documents() const { return documents_; }
    const std::vector<Document>& closed() const { return closed_; }
    DocumentId active_id() const { return active_; }
    const Document* document(DocumentId id) const {
        auto found = std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) { return tab.id == id; });
        return found == documents_.end() ? nullptr : &*found;
    }
    const Document* document(const Location& location) const {
        auto found = std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) {
            return same_document(tab.location, location);
        });
        return found == documents_.end() ? nullptr : &*found;
    }
    const Document* retained_review() const {
        if (const auto* open = document(review()); open && open->files) return open;
        auto found = std::find_if(closed_.rbegin(), closed_.rend(), [&](const auto& tab) {
            return same_document(tab.location, review());
        });
        return found == closed_.rend() ? document(review()) : &*found;
    }
    const Document* recent(Slot slot) const {
        const Document* found = nullptr;
        for (const auto& tab : documents_) {
            if (std::holds_alternative<SourceLocation>(tab.location) != (slot == Slot::Source)) continue;
            if (!found || tab.lastActivated > found->lastActivated) found = &tab;
        }
        return found;
    }
    const ReviewLocation& review() const {
        if (const auto* value = std::get_if<ReviewLocation>(&location())) return *value;
        if (const auto& origin = std::get<SourceLocation>(location()).origin) return *origin;
        if (const auto* tab = recent(Slot::Review)) return std::get<ReviewLocation>(tab->location);
        static const ReviewLocation working;
        return working;
    }
    const SourceLocation* source() const {
        if (const auto* source = std::get_if<SourceLocation>(&location())) return source;
        const auto* tab = recent(Slot::Source);
        return tab ? &std::get<SourceLocation>(tab->location) : nullptr;
    }
    Slot active() const { return std::holds_alternative<SourceLocation>(location()) ? Slot::Source : Slot::Review; }
    std::uint64_t generation() const { return generation_; }
    const std::vector<Visit>& history() const { return history_; }
    size_t history_index() const { return index_; }
    const Location& location() const { return document(active_)->location; }
private:
    void reset() {
        auto next = generation_ + 1;
        *this = {};
        generation_ = next;
    }
    bool open(Location next, bool reviewing = false, OpenMode mode = OpenMode::Preview) {
        Visit visit{std::move(next), reviewing};
        if (history_[index_].reviewing == visit.reviewing &&
            same_document(history_[index_].location, visit.location) && location() == visit.location) {
            if (mode == OpenMode::Keep) keep(active_);
            return false;
        }
        history_.resize(index_ + 1);
        history_.push_back(visit);
        if (history_.size() > 256) history_.erase(history_.begin());
        index_ = history_.size() - 1;
        select(visit.location, mode);
        return true;
    }
    bool reorder(DocumentId id, size_t insertion) {
        auto found = std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) { return tab.id == id; });
        if (found == documents_.end() || insertion > documents_.size()) return false;
        const size_t index = static_cast<size_t>(found - documents_.begin());
        if (insertion == index || insertion == index + 1) return false;
        if (insertion < index) std::rotate(documents_.begin() + insertion, found, found + 1);
        else std::rotate(found, found + 1, documents_.begin() + insertion);
        lastClick_.reset();
        return true;
    }
    bool close(DocumentId id, bool reviewing) {
        auto found = std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) { return tab.id == id; });
        if (found == documents_.end()) return false;
        if (documents_.size() == 1 && found->location == Location{ReviewLocation{}}) return false;
        const auto index = static_cast<size_t>(found - documents_.begin());
        const bool wasActive = id == active_;
        closed_.push_back(std::move(*found));
        if (closed_.size() > 20) closed_.erase(closed_.begin());
        documents_.erase(found);
        if (!wasActive) return false;
        if (documents_.empty()) documents_.push_back({DocumentId{nextId_++}});
        active_ = documents_[std::min(index, documents_.size() - 1)].id;
        auto next = location();
        if (const auto* review = std::get_if<ReviewLocation>(&next);
            review && std::holds_alternative<WorkingChanges>(review->destination)) reviewing = true;
        ++generation_;
        open(std::move(next), reviewing);
        return true;
    }
    bool reopen(bool reviewing) {
        if (closed_.empty()) return false;
        auto restored = std::move(closed_.back());
        restored.preview = false;
        closed_.pop_back();
        auto next = restored.location;
        auto existing = std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) {
            return same_document(tab.location, next);
        });
        if (existing == documents_.end()) documents_.push_back(std::move(restored));
        else {
            if (!existing->files && restored.files) existing->files = std::move(restored.files);
            if (existing->subject.empty()) existing->subject = std::move(restored.subject);
            if (!existing->anchor && restored.anchor) {
                existing->anchor = std::move(restored.anchor);
                existing->restoreAnchor = restored.restoreAnchor;
            }
        }
        return open(std::move(next), reviewing, OpenMode::Keep);
    }
    bool step(int direction) {
        if (direction == 0 || (direction < 0 && index_ == 0) ||
            (direction > 0 && index_ + 1 >= history_.size())) return false;
        if (direction < 0) --index_;
        else ++index_;
        const auto visit = history_[index_];
        select(visit.location);
        current().anchor = visit.anchor;
        current().restoreAnchor = visit.anchor.has_value();
        if (auto* source = std::get_if<SourceLocation>(&current().location); source && visit.anchor) {
            source->line = visit.anchor->line;
            source->column = visit.anchor->column;
        }
        return true;
    }
    void clear_source_reveal() {
        auto* source = std::get_if<SourceLocation>(&current().location);
        if (!source) return;
        source->line = 0;
        source->column = 1;
    }
    void coalesce_resolved_document() {
        auto& resolved = current();
        auto found = std::find_if(documents_.begin(), documents_.end(), [&](const auto& tab) {
            return tab.id != active_ && same_document(tab.location, resolved.location);
        });
        if (found != documents_.end()) {
            auto replaced = active_;
            found->location = resolved.location;
            found->lastActivated = resolved.lastActivated;
            found->preview = found->preview && resolved.preview;
            if (!found->files) found->files = std::move(resolved.files);
            if (found->subject.empty()) found->subject = std::move(resolved.subject);
            if (resolved.anchor) {
                found->anchor = std::move(resolved.anchor);
                found->restoreAnchor = resolved.restoreAnchor;
            }
            active_ = found->id;
            std::erase_if(documents_, [&](const auto& tab) { return tab.id == replaced; });
        }
        history_[index_].location = current().location;
    }
    void resolve_origins(const ReviewDestination& before, const ReviewDestination& after) {
        auto resolve = [&](Location& location) {
            if (auto* source = std::get_if<SourceLocation>(&location); source && source->origin && source->origin->destination == before) {
                source->origin->destination = after;
                if (source->originAnchor) source->originAnchor->revision = scope(*source->origin);
            }
        };
        for (auto& document : documents_) resolve(document.location);
        for (auto& document : closed_) resolve(document.location);
        for (auto& visit : history_) resolve(visit.location);
    }
    bool resolve_source(std::uint64_t generation, const std::string& oid) {
        if (generation != generation_ || active() != Slot::Source || !is_object_id(oid)) return false;
        auto& source = std::get<SourceLocation>(current().location);
        if (!std::holds_alternative<RevisionQuery>(source.destination.revision)) return false;
        source.destination.revision = ObjectId{oid};
        coalesce_resolved_document();
        return true;
    }
    bool resolve_comparison(std::uint64_t generation, const std::string& before, const std::string& after) {
        if (generation != generation_ || active() != Slot::Review || !is_object_id(before) || !is_object_id(after)) return false;
        auto& review = std::get<ReviewLocation>(current().location);
        auto* destination = std::get_if<ComparisonReview>(&review.destination);
        if (!destination) return false;
        const auto previous = review.destination;
        *destination = {ObjectId{before}, ObjectId{after}};
        resolve_origins(previous, review.destination);
        coalesce_resolved_document();
        return true;
    }
    bool resolve_review(std::uint64_t generation, const std::string& commit, const std::string& parent) {
        if (generation != generation_ || active() != Slot::Review || !is_object_id(commit)) return false;
        auto& review = std::get<ReviewLocation>(current().location);
        auto* destination = std::get_if<CommitReview>(&review.destination);
        if (!destination) return false;
        const auto previous = review.destination;
        destination->commit = ObjectId{commit};
        if (destination->parent && is_object_id(parent)) destination->parent = ObjectId{parent};
        resolve_origins(previous, review.destination);
        coalesce_resolved_document();
        return true;
    }
};

enum class FocusPolicy { Document, Caller };

struct NavigationEffect {
    bool changed = false;
    bool dismissedPanel = false;
    FocusPolicy focus = FocusPolicy::Document;
    std::optional<bool> reviewing;
};

struct RequestStamp {
    std::string repository;
    Location document;
    std::string key;
    std::uint64_t generation = 0;
    unsigned dataGeneration = 0;
    bool operator==(const RequestStamp&) const = default;
};

}

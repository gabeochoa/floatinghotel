#pragma once

#include <algorithm>
#include <cstdint>
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
    bool operator==(const SourceLocation&) const = default;
};
using Location = std::variant<ReviewLocation, SourceLocation>;

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

struct Visit {
    Location location = ReviewLocation{};
    bool reviewing = false;
    bool operator==(const Visit&) const = default;
};

class ReadingWorkspace {
    friend struct ::navigation;
    ReviewLocation review_;
    std::optional<SourceLocation> source_;
    Slot active_ = Slot::Review;
    std::vector<Visit> history_{{}};
    size_t index_ = 0;
    std::uint64_t generation_ = 0;

    void select(const Location& location) {
        if (const auto* value = std::get_if<ReviewLocation>(&location)) {
            review_ = *value;
            active_ = Slot::Review;
        } else {
            source_ = std::get<SourceLocation>(location);
            active_ = Slot::Source;
        }
        ++generation_;
    }
public:
    const ReviewLocation& review() const { return review_; }
    const std::optional<SourceLocation>& source() const { return source_; }
    Slot active() const { return active_; }
    std::uint64_t generation() const { return generation_; }
    const std::vector<Visit>& history() const { return history_; }
    size_t history_index() const { return index_; }
    Location location() const {
        if (active_ == Slot::Source && source_) return *source_;
        return review_;
    }
private:
    void reset() {
        auto next = generation_ + 1;
        *this = {};
        generation_ = next;
    }
    bool open(Location next, bool reviewing = false) {
        Visit visit{std::move(next), reviewing};
        if (history_[index_] == visit && location() == visit.location) return false;
        history_.resize(index_ + 1);
        history_.push_back(visit);
        if (history_.size() > 256) history_.erase(history_.begin());
        index_ = history_.size() - 1;
        select(visit.location);
        return true;
    }
    bool activate(Slot slot, bool reviewing = false) {
        if (slot == Slot::Source) return source_ && open(*source_, reviewing);
        return open(review_, reviewing);
    }
    bool close_source(bool reviewing = false) {
        if (!source_) return false;
        if (active_ == Slot::Source) activate(Slot::Review, reviewing);
        source_.reset();
        return true;
    }
    bool step(int direction) {
        if (direction == 0 || (direction < 0 && index_ == 0) ||
            (direction > 0 && index_ + 1 >= history_.size())) return false;
        if (direction < 0) --index_;
        else ++index_;
        select(history_[index_].location);
        return true;
    }
    bool return_to_review(bool reviewing = false) {
        return open(source_ && source_->origin ? *source_->origin : review_, reviewing);
    }
    void clear_source_reveal() {
        if (active_ != Slot::Source || !source_) return;
        source_->line = 0;
        history_[index_].location = *source_;
    }
    bool resolve_source(std::uint64_t generation, const std::string& oid) {
        if (generation != generation_ || active_ != Slot::Source || !source_ || !is_object_id(oid)) return false;
        if (!std::holds_alternative<RevisionQuery>(source_->destination.revision)) return false;
        source_->destination.revision = ObjectId{oid};
        history_[index_].location = *source_;
        return true;
    }
    bool resolve_comparison(std::uint64_t generation, const std::string& before, const std::string& after) {
        if (generation != generation_ || active_ != Slot::Review || !is_object_id(before) || !is_object_id(after)) return false;
        auto* destination = std::get_if<ComparisonReview>(&review_.destination);
        if (!destination) return false;
        *destination = {ObjectId{before}, ObjectId{after}};
        history_[index_].location = review_;
        return true;
    }
    bool resolve_review(std::uint64_t generation, const std::string& commit, const std::string& parent) {
        if (generation != generation_ || active_ != Slot::Review || !is_object_id(commit)) return false;
        auto* destination = std::get_if<CommitReview>(&review_.destination);
        if (!destination) return false;
        destination->commit = ObjectId{commit};
        if (destination->parent && is_object_id(parent)) destination->parent = ObjectId{parent};
        history_[index_].location = review_;
        return true;
    }
};

struct NavigationEffect {
    bool changed = false;
    bool dismissedPanel = false;
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

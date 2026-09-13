#pragma once

#include "reading_workspace.h"
#include <cmath>
#include <nlohmann/json.hpp>

namespace reading {

struct SavedDocument {
    Location location;
    std::string subject;
    std::uint64_t lastActivated = 0;
    std::optional<ReadingAnchor> anchor;
};

struct ReadingSession {
    std::vector<SavedDocument> documents;
    size_t active = 0;
};

inline std::string anchor_revision(const Location& location) {
    if (const auto* source = std::get_if<SourceLocation>(&location)) return revision_text(source->destination.revision);
    return scope(std::get<ReviewLocation>(location));
}

inline bool unresolved_destination(const Location& location) {
    if (const auto* source = std::get_if<SourceLocation>(&location))
        return std::holds_alternative<RevisionQuery>(source->destination.revision);
    return std::visit([](const auto& destination) {
        using T = std::decay_t<decltype(destination)>;
        if constexpr (std::is_same_v<T, WorkingChanges>) return false;
        else if constexpr (std::is_same_v<T, CommitReview>)
            return std::holds_alternative<RevisionQuery>(destination.commit) ||
                (destination.parent && std::holds_alternative<RevisionQuery>(*destination.parent));
        else return std::holds_alternative<RevisionQuery>(destination.before) || std::holds_alternative<RevisionQuery>(destination.after);
    }, std::get<ReviewLocation>(location).destination);
}

inline ReadingSession save_session(const ReadingWorkspace& workspace) {
    ReadingSession session;
    std::uint64_t mostRecent = 0;
    bool foundActive = false;
    for (const auto& document : workspace.documents()) {
        if (document.preview) continue;
        if (document.id == workspace.active_id()) { session.active = session.documents.size(); foundActive = true; }
        else if (!foundActive && document.lastActivated > mostRecent) session.active = session.documents.size();
        mostRecent = std::max(mostRecent, document.lastActivated);
        session.documents.push_back({document.location, document.subject, document.lastActivated, document.anchor});
    }
    return session;
}

template<class... T>
inline nlohmann::json encode_revision(const std::variant<T...>& revision) {
    return std::visit([](const auto& value) -> nlohmann::json {
        using V = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<V, WorkingTree>) return {{"kind", "working"}};
        else if constexpr (std::is_same_v<V, Index>) return {{"kind", "index"}};
        else if constexpr (std::is_same_v<V, ObjectId>) return {{"kind", "object"}, {"value", value.value}};
        else return {{"kind", "query"}, {"value", value.text}};
    }, revision);
}

inline SourceRevision decode_revision(const nlohmann::json& value) {
    const auto kind = value.at("kind").get<std::string>();
    if (kind == "working") return WorkingTree{};
    if (kind == "index") return Index{};
    const auto text = value.at("value").get<std::string>();
    if (kind == "object" && is_object_id(text)) return ObjectId{text};
    if (kind == "query" && !text.empty()) return RevisionQuery{text};
    throw std::invalid_argument("Invalid saved revision");
}

inline HistoricalRevision decode_historical(const nlohmann::json& value) {
    const auto revision = decode_revision(value);
    if (const auto* object = std::get_if<ObjectId>(&revision)) return *object;
    if (const auto* query = std::get_if<RevisionQuery>(&revision)) return *query;
    throw std::invalid_argument("Historical revision required");
}

inline nlohmann::json encode_review(const ReviewLocation& review) {
    auto result = std::visit([](const auto& destination) -> nlohmann::json {
        using T = std::decay_t<decltype(destination)>;
        if constexpr (std::is_same_v<T, WorkingChanges>) return {{"kind", "working"}, {"staged", destination.staged}};
        else if constexpr (std::is_same_v<T, CommitReview>) {
            nlohmann::json value{{"kind", "commit"}, {"commit", encode_revision(destination.commit)}};
            if (destination.parent) value["parent"] = encode_revision(*destination.parent);
            return value;
        } else return {{"kind", "comparison"}, {"before", encode_revision(destination.before)}, {"after", encode_revision(destination.after)}};
    }, review.destination);
    result["file"] = review.file;
    return result;
}

inline ReviewLocation decode_review(const nlohmann::json& value) {
    const auto kind = value.at("kind").get<std::string>();
    ReviewLocation review;
    review.file = value.value("file", std::string{});
    if (kind == "working") review.destination = WorkingChanges{value.value("staged", false)};
    else if (kind == "commit") {
        CommitReview commit{decode_historical(value.at("commit")), {}};
        if (value.contains("parent")) commit.parent = decode_historical(value.at("parent"));
        review.destination = std::move(commit);
    } else if (kind == "comparison") review.destination = ComparisonReview{decode_historical(value.at("before")), decode_historical(value.at("after"))};
    else throw std::invalid_argument("Invalid saved review");
    return review;
}

inline nlohmann::json encode_location(const Location& location) {
    if (const auto* review = std::get_if<ReviewLocation>(&location)) return {{"review", encode_review(*review)}};
    const auto& source = std::get<SourceLocation>(location);
    nlohmann::json value{{"path", source.destination.path}, {"revision", encode_revision(source.destination.revision)}, {"line", source.line}};
    if (source.origin) value["origin"] = encode_review(*source.origin);
    return {{"source", std::move(value)}};
}

inline Location decode_location(const nlohmann::json& value) {
    if (value.contains("review")) return decode_review(value.at("review"));
    const auto& source = value.at("source");
    SourceLocation location{{source.at("path").get<std::string>(), decode_revision(source.at("revision"))}, source.value("line", 0), {}};
    if (location.destination.path.empty() || location.line < 0) throw std::invalid_argument("Invalid saved source");
    if (source.contains("origin")) location.origin = decode_review(source.at("origin"));
    return location;
}

inline nlohmann::json encode_session(const ReadingSession& session) {
    nlohmann::json documents = nlohmann::json::array();
    for (const auto& document : session.documents) {
        nlohmann::json value{{"location", encode_location(document.location)}, {"subject", document.subject}, {"recent", document.lastActivated}};
        if (document.anchor) {
            const auto& anchor = *document.anchor;
            value["anchor"] = {{"path", anchor.path}, {"revision", anchor.revision}, {"side", anchor.side == DiffSide::Before ? "before" : "after"},
                {"line", anchor.line}, {"column", anchor.column}, {"fraction", anchor.viewportFraction}, {"sign", std::string(1, anchor.sign)}};
        }
        documents.push_back(std::move(value));
    }
    return {{"version", 1}, {"active", session.active}, {"documents", std::move(documents)}};
}

inline std::optional<ReadingSession> decode_session(const nlohmann::json& value) {
    try {
        if (value.at("version").get<int>() != 1) return {};
        const auto& documents = value.at("documents");
        if (!documents.is_array()) return {};
        const size_t active = value.value("active", size_t{0});
        ReadingSession session;
        for (size_t i = 0; i < documents.size(); ++i) {
            try {
                const auto& source = documents[i];
                SavedDocument document{decode_location(source.at("location")), source.value("subject", std::string{}), source.value("recent", std::uint64_t{0}), {}};
                if (std::any_of(session.documents.begin(), session.documents.end(), [&](const auto& existing) { return same_document(existing.location, document.location); })) continue;
                if (source.contains("anchor")) {
                    const auto& a = source.at("anchor");
                    const auto side = a.at("side").get<std::string>();
                    const auto sign = a.at("sign").get<std::string>();
                    ReadingAnchor anchor{a.at("path").get<std::string>(), a.at("revision").get<std::string>(), side == "before" ? DiffSide::Before : DiffSide::After,
                        a.at("line").get<int>(), a.at("column").get<int>(), a.at("fraction").get<float>(), sign.empty() ? ' ' : sign.front()};
                    if (!anchor.path.empty() && anchor.revision == anchor_revision(document.location) &&
                        (side == "before" || side == "after") && (sign == " " || sign == "+" || sign == "-") &&
                        anchor.line > 0 && anchor.column > 0 && std::isfinite(anchor.viewportFraction) && anchor.viewportFraction >= 0.f && anchor.viewportFraction <= 1.f)
                        document.anchor = std::move(anchor);
                }
                if (i == active) session.active = session.documents.size();
                session.documents.push_back(std::move(document));
            } catch (const std::exception&) {}
        }
        if (session.documents.empty()) return {};
        return session;
    } catch (const std::exception&) { return {}; }
}

}

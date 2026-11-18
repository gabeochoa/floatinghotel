#pragma once

// Language-server navigation for the caret in a working-tree source document:
// go to definition, references, the type under the caret, callers and callees.
// One request at a time per repository tab; switching documents cancels it at
// the server, and an answer about a file that changed meanwhile is dropped as
// stale. Revisions, the index and diffs keep text navigation only.

#include <filesystem>
#include <sstream>

#include "../ecs/components.h"
#include "../lsp/semantic.h"
#include "../util/navigation.h"
#include "../util/search_results.h"
#include "repo_search.h"

namespace ecs {

inline void semantic_notice(const std::string& message, MenuComponent::Notice::Kind kind = MenuComponent::Notice::Kind::Info) {
    if (auto* menu = find_singleton<MenuComponent>()) menu->pendingToasts.push_back({message, kind});
}

inline std::optional<std::pair<std::filesystem::file_time_type, std::uintmax_t>> semantic_file_stamp(const RepoComponent& repo,
                                                                                                    const std::string& path) {
    std::error_code ec;
    const auto file = std::filesystem::path(repo.repoPath) / path;
    auto written = std::filesystem::last_write_time(file, ec);
    if (ec) return std::nullopt;
    auto size = std::filesystem::file_size(file, ec);
    if (ec) return std::nullopt;
    return std::pair{written, size};
}

inline void request_semantic(RepoComponent& repo, semantic::Kind kind) {
    auto* document = repo.workspace().document(repo.workspace().active_id());
    if (source_tab_active(repo) && !repo.fullFileRevision().empty()) {
        semantic_notice("Language servers read the working tree; this document shows " + repo.fullFileRevision().substr(0, 12));
        return;
    }
    if (!source_tab_active(repo) || !document || !document->caret) {
        semantic_notice("Place the caret on a symbol in a source file first");
        return;
    }
    std::string missing;
    if (!semantic::server_for(repo.fullFilePath(), &missing)) {
        semantic_notice(missing.empty() ? "No language server handles this file type" : missing + " (not found on PATH)");
        return;
    }
    if (!repo.semanticSession) repo.semanticSession = std::make_shared<semantic::Session>(repo.repoPath);
    std::string text;
    if (document->editMode && document->editSeeded) {
        for (size_t i = 0; i < document->editLines.size(); ++i)
            text += document->editLines[i] + (i + 1 < document->editLines.size() || document->editTrailingNewline ? "\n" : "");
    }
    repo.semanticDocument = document->id.value;
    repo.semanticPath = repo.fullFilePath();
    repo.semanticEdited = document->editMode;
    repo.semanticStamp = semantic_file_stamp(repo, repo.semanticPath);
    repo.semanticSymbol = ui::selected_identifier(repo);
    repo.semanticFuture = repo.semanticSession->run(kind, repo.fullFilePath(), std::move(text), document->caret->line, document->caret->column);
}

// Results the user picks from go to the repository-search panel: same list,
// grouping, preview and Back/Forward as a text search, labelled with where
// they came from.
inline void show_semantic_results(RepoComponent& repo, const semantic::Outcome& outcome) {
    std::vector<SearchMatch> matches;
    for (const auto& location : outcome.locations) {
        SearchMatch match;
        match.file = location.path;
        match.line = location.line;
        match.text = location.label.empty() ? location.text : location.label + "  " + location.text;
        matches.push_back(std::move(match));
    }
    clear_repo_search(repo);
    repo.repoSearchQuery = repo.semanticSymbol;
    repo.repoSearchMatching = {.regularExpression = false, .caseSensitive = true, .wholeWord = true};
    open_repo_search(repo);
    // The panel searches when its text changes; these results are not a search.
    repo.repoSearchObservedText = {repo.repoSearchQuery, repo.repoSearchIncludeGlob, repo.repoSearchExcludeGlob};
    repo.repoSearchSubmittedQuery = repo.repoSearchQuery;
    repo.repoSearchPath = repo.repoPath;
    repo.repoSearchDue.reset();
    repo.repoSearchResults = std::move(matches);
    repo.repoSearchGroups = search_results::group(repo.repoSearchResults);
    repo.repoSearchRebuildRows = true;
    repo.repoSearchSource = std::to_string(outcome.locations.size()) + " " + semantic::describe(outcome.kind) +
        (repo.semanticSymbol.empty() ? "" : " of " + repo.semanticSymbol) + " · " + outcome.server;
}

inline void open_semantic_location(RepoComponent& repo, const semantic::Location& location) {
    auto target = reading::source(location.path, {}, location.line, repo.workspace().review());
    navigation::click(repo, target, true, reading::ClickRegion::Search);
    repo.pendingCaret = reading::CodePosition{location.path, reading::DiffSide::After, location.line, location.column};
}

inline void poll_semantic(RepoComponent& repo) {
    if (!repo.semanticFuture.valid()) return;
    // A different document is active: the question no longer applies.
    if (repo.workspace().active_id().value != repo.semanticDocument) {
        repo.semanticFuture = {};
        return;
    }
    if (repo.semanticFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto outcome = repo.semanticFuture.get();
    repo.semanticFuture = {};
    if (outcome.error == "cancelled") return;
    if (!outcome.error.empty()) {
        semantic_notice(outcome.error, MenuComponent::Notice::Kind::Error);
        return;
    }
    if (!repo.semanticEdited && semantic_file_stamp(repo, repo.semanticPath) != repo.semanticStamp) {
        semantic_notice(repo.semanticPath + " changed while " + outcome.server + " answered; run it again", MenuComponent::Notice::Kind::Error);
        return;
    }
    switch (outcome.kind) {
        case semantic::Kind::Hover:
            if (outcome.hover.empty()) { semantic_notice("No type information here"); return; }
            repo.hoverInfo = outcome.hover;
            repo.hoverAnchor = {repo.semanticPath, reading::DiffSide::After, 1, 1};
            if (const auto* document = repo.workspace().document(repo.workspace().active_id());
                document && document->caret) repo.hoverAnchor = *document->caret;
            return;
        case semantic::Kind::Definition:
            if (outcome.locations.size() == 1) { open_semantic_location(repo, outcome.locations.front()); return; }
            break;
        case semantic::Kind::References:
        case semantic::Kind::Incoming:
        case semantic::Kind::Outgoing:
            break;
    }
    if (outcome.locations.empty()) {
        semantic_notice("No " + semantic::describe(outcome.kind) + " found" + (repo.semanticSymbol.empty() ? "" : " for " + repo.semanticSymbol));
        return;
    }
    show_semantic_results(repo, outcome);
}

}  // namespace ecs

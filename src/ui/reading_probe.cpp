#include "reading_probe.h"
#include "../util/document_titles.h"

#include "../ecs/ui_imports.h"
#include "layout_dump.h"
#include "../ecs/query_helpers.h"
#include "../git/blob_page_cache.h"
#include "../git/commit_patch_cache.h"
#include <chrono>

namespace reading_probe {

using Clock = std::chrono::steady_clock;

struct Probe {
    std::string label;
    std::string kind;
    std::string path;
    std::string revision;
    Clock::time_point armed = Clock::now();
    std::optional<Clock::time_point> dispatched;
    std::optional<double> selectionMs;
    std::optional<double> readyMs;
    std::pair<size_t, size_t> blobBefore;
    std::pair<size_t, size_t> patchBefore;
};

static Probe probe;
static bool checkpointPending = false;
static std::optional<Clock::time_point> pathWaitStart;

bool checkpoint_pending() { return checkpointPending || pathWaitStart.has_value(); }

void input_dispatched() {
    if (!probe.label.empty() && !probe.dispatched) probe.dispatched = Clock::now();
}

static size_t owned_content_bytes(const std::vector<ecs::FileDiff>& files) {
    size_t bytes = files.capacity() * sizeof(ecs::FileDiff);
    for (const auto& file : files) {
        for (const auto* value : {&file.filePath, &file.oldPath, &file.oldMode, &file.newMode,
                &file.oldObject, &file.newObject}) bytes += value->capacity() + 1;
        bytes += file.hunks.capacity() * sizeof(ecs::DiffHunk);
        for (const auto& hunk : file.hunks) {
            bytes += hunk.header.capacity() + 1 + hunk.lines.capacity() * sizeof(std::string);
            for (const auto& line : hunk.lines) bytes += line.capacity() + 1;
        }
    }
    return bytes;
}

void rendered() {
    if (!probe.dispatched || probe.readyMs) return;
    auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
    if (!repo) return;
    bool selected = probe.kind == "source"
        ? ecs::source_tab_active(*repo) && repo->fullFilePath() == probe.path && repo->fullFileRevision() == probe.revision
        : !ecs::source_tab_active(*repo) && repo->selectedCommitHash() == probe.revision &&
            (probe.path.empty() || repo->diffTargetFile() == probe.path || repo->selectedFilePath() == probe.path);
    if (!selected) return;
    auto* detail = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>();
    const std::string headingName = probe.kind == "source" ? "full_file_revision" : "commit_detail_subject";
    const std::string headingText = probe.kind == "source"
        ? probe.path + " @ " + (probe.revision.empty() ? "working tree" : probe.revision)
        : detail ? detail->entry.subject : "";
    bool headingVisible = false;
    for (afterhours::Entity& entity : afterhours::EntityQuery<>(afterhours::ui::UICollectionHolder::get().collection,
             {.force_merge = true, .ignore_temp_warning = true})
             .whereHasComponent<afterhours::ui::UIComponent>()
             .whereHasComponent<afterhours::ui::UIComponentDebug>()
             .whereHasComponent<afterhours::ui::HasLabel>().gen()) {
        if (!entity.get<afterhours::ui::UIComponent>().was_rendered_to_screen ||
            entity.get<afterhours::ui::UIComponentDebug>().name() != headingName ||
            entity.get<afterhours::ui::HasLabel>().label != headingText) continue;
        auto rect = ui::visible_rect(entity);
        headingVisible = rect.width > 0.f && rect.height > 0.f;
        if (headingVisible) break;
    }
    if (!headingVisible) return;
    double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - *probe.dispatched).count();
    if (!probe.selectionMs) probe.selectionMs = elapsed;
    bool ready = false;
    if (probe.kind == "source") ready = !repo->fullFileFuture.valid() && !repo->fullFileDiff.empty() && repo->fullFileError.empty();
    else if (probe.revision.empty()) ready = !repo->isRefreshing && !repo->refreshRequested;
    else if (detail)
        ready = detail->cachedCommitHash == probe.revision && !detail->patchFuture.valid() &&
            !detail->commitDetailDiff.empty() && detail->commitDetailError.empty();
    if (ready) probe.readyMs = elapsed;
}

struct Handle : afterhours::System<afterhours::testing::PendingE2ECommand> {
    std::filesystem::path directory;
    explicit Handle(std::filesystem::path path) : directory(std::move(path)) {}

    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed()) return;
        if (cmd.is("wait_for_path")) {
            if (!cmd.has_args(1) || cmd.arg(0).empty()) { cmd.fail("wait_for_path requires a path"); return; }
            const auto now = Clock::now();
            if (!pathWaitStart) pathWaitStart = now;
            std::error_code error;
            if (std::filesystem::exists(directory / cmd.arg(0), error)) {
                pathWaitStart.reset();
                cmd.consume();
            } else if (error || now - *pathWaitStart > std::chrono::seconds(10)) {
                pathWaitStart.reset();
                cmd.fail(error ? error.message() : "Timed out waiting for " + cmd.arg(0));
            } else {
                cmd.frames_alive = 0;
                cmd.retry();
            }
            return;
        }
        if (cmd.is("workspace_checkpoint")) {
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            auto* detail = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>();
            if (!repo || cmd.args.size() != 2 || cmd.arg(1).empty() ||
                cmd.arg(1).find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
                cmd.fail("workspace_checkpoint requires document count and label");
                return;
            }
            if (std::to_string(repo->workspace().documents().size()) != cmd.arg(0)) {
                cmd.fail("Unexpected document count");
                return;
            }
            const bool source = ecs::source_tab_active(*repo);
            if ((source && detail && (!detail->commitDetailDiff.empty() || !detail->commitDetailBody.empty())) ||
                (!source && (!repo->fullFileDiff.empty() || !repo->fullFileBytes.empty() || !repo->fullFileDecodedText.empty()))) {
                cmd.fail("Inactive document retained a rendering payload");
                return;
            }
            nlohmann::json tabs = nlohmann::json::array();
            const auto titles = reading::document_titles(repo->workspace().documents());
            size_t titleIndex = 0;
            for (const auto& tab : repo->workspace().documents()) {
                const auto& title = titles[titleIndex++];
                nlohmann::json value{{"id", tab.id.value}, {"preview", tab.preview},
                    {"title", title.label}, {"badge", title.badge}, {"tooltip", title.tooltip}};
                if (const auto* file = std::get_if<reading::SourceLocation>(&tab.location)) {
                    value["kind"] = "source";
                    value["path"] = file->destination.path;
                    value["revision"] = reading::revision_text(file->destination.revision);
                } else {
                    const auto& review = std::get<reading::ReviewLocation>(tab.location);
                    value["kind"] = "review";
                    value["revision"] = reading::scope(review);
                    value["path"] = review.file;
                }
                value["details_expanded"] = tab.detailsExpanded;
                value["find"] = {{"open", tab.find.open}, {"query", tab.find.query}, {"index", tab.find.index}};
                if (tab.find.position) value["find"]["position"] = {
                    {"path", tab.find.position->path}, {"line", tab.find.position->line},
                    {"column", tab.find.position->column}, {"sign", std::string(1, tab.find.position->sign)}};
                tabs.push_back(std::move(value));
            }
            try {
                std::filesystem::create_directories(directory);
                std::ofstream output(directory / (cmd.arg(1) + ".workspace.json"));
                output.exceptions(std::ios::failbit | std::ios::badbit);
                output << nlohmann::json{{"active", repo->workspace().active_id().value},
                    {"source_find", {{"matches", repo->sourceFind.result.matches.size()}, {"loading", repo->sourceFind.future.valid()},
                        {"match_bytes", repo->sourceFind.result.matches.capacity() * sizeof(ecs::SourceFindMatch)},
                        {"limited", repo->sourceFind.result.limited}, {"error", repo->sourceFind.result.error},
                        {"scanned_bytes", repo->sourceFind.result.scannedBytes}, {"max_page_bytes", repo->sourceFind.result.maxPageBytes},
                        {"page_line", repo->fullFilePage.begin.line}, {"page_column", repo->fullFilePage.begin.column},
                        {"page_offset", repo->fullFilePage.begin.offset}}},
                    {"tabs", tabs}, {"history_index", repo->workspace().history_index()}, {"history", [&] {
                        auto visits = nlohmann::json::array();
                        for (const auto& visit : repo->workspace().history()) {
                            nlohmann::json value{{"location", reading::encode_location(visit.location)}, {"reviewing", visit.reviewing}};
                            if (visit.anchor) {
                                const auto& anchor = *visit.anchor;
                                value["anchor"] = {{"path", anchor.path}, {"revision", anchor.revision}, {"line", anchor.line},
                                    {"column", anchor.column}, {"side", anchor.side == reading::DiffSide::Before ? "before" : "after"},
                                    {"fraction", anchor.viewportFraction}, {"sign", std::string(1, anchor.sign)}};
                            }
                            visits.push_back(std::move(value));
                        }
                        return visits;
                    }()}, {"review", [&] {
                        const auto* review = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
                        return review ? nlohmann::json{{"cursor", review->cursor}, {"hunks", review->hunkCount},
                            {"approve_pending", review->cursorApprove}, {"comment_pending", review->cursorComment},
                            {"approved", review->approvedHunks.size()}, {"composing", review->composingKey},
                            {"draft", review->composingText}, {"comments", review->comments.size()}} : nlohmann::json{};
                    }()}, {"trees", [&] {
                        auto value = [](const file_tree::NavigationState& state) {
                            return nlohmann::json{{"path", state.path}, {"reveal_path", state.revealPath},
                                {"pending_focus", state.pendingFocus}, {"pending_reveal", state.pendingReveal},
                                {"entity", state.revealEntity}, {"generation", state.navigationGeneration.value_or(0)},
                                {"context", state.context}};
                        };
                        return nlohmann::json{{"review", value(repo->reviewTreeNavigation)}, {"files", value(repo->filesTreeNavigation)},
                            {"generation", repo->workspace().generation()}};
                    }()}, {"search", {{"open", repo->repoSearchOpen}, {"query", repo->repoSearchQuery},
                        {"revision", repo->repoSearchScope.revision}, {"submissions", repo->repoSearchGeneration},
                        {"matches", repo->repoSearchResults.size()}, {"scroll", repo->repoSearchScroll},
                        {"pending", repo->repoSearchDue.has_value()}, {"loading", repo->repoSearchFuture.valid()},
                        {"submitted_query", repo->repoSearchSubmittedQuery}, {"captured_bytes", repo->repoSearchCapturedBytes},
                        {"truncated", repo->repoSearchTruncated},
                        {"visible_rows", repo->repoSearchRows.size()},
                        {"highlight_bytes", repo->repoSearchResults.size() * sizeof(std::bitset<512>)},
                        {"row_metadata_bytes", repo->repoSearchRows.capacity() * sizeof(ecs::SearchResultRow)},
                        {"groups", [&] {
                            auto groups = nlohmann::json::array();
                            for (const auto& group : repo->repoSearchGroups) groups.push_back({{"file", group.file},
                                {"revision", group.revision}, {"matches", group.matches.size()}, {"collapsed", group.collapsed}});
                            return groups;
                        }()},
                        {"selected", repo->repoSearchSelected ? nlohmann::json(*repo->repoSearchSelected) : nlohmann::json{}},
                        {"include", repo->repoSearchIncludeGlob}, {"exclude", repo->repoSearchExcludeGlob},
                        {"changed_only", repo->repoSearchChangedOnly}}}, {"commit_log", {{"count", repo->commitLog.size()}, {"has_more", repo->commitLogHasMore},
                        {"loading", repo->commitLogPage.requested || repo->commitLogPage.future.valid()},
                        {"error", repo->commitLogPage.error}, {"hashes", [&] {
                            auto hashes = nlohmann::json::array();
                            for (const auto& commit : repo->commitLog) hashes.push_back(commit.hash);
                            return hashes;
                        }()}}}, {"inactive_payloads_empty", true}}.dump(2) << '\n';
                cmd.consume();
            } catch (const std::exception& error) { cmd.fail(error.what()); }
        } else if (cmd.is("reading_probe")) {
            if (cmd.args.size() != 4 || cmd.arg(0).empty() ||
                cmd.arg(0).find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos ||
                (cmd.arg(1) != "source" && cmd.arg(1) != "review")) {
                cmd.fail("reading_probe requires label, source|review, path|- and revision|-");
                return;
            }
            if (!probe.label.empty()) { cmd.fail("Previous reading probe was not checked"); return; }
            probe = {cmd.arg(0), cmd.arg(1), cmd.arg(2) == "-" ? "" : cmd.arg(2), cmd.arg(3) == "-" ? "" : cmd.arg(3)};
            probe.blobBefore = git::blob_page_cache().activity();
            probe.patchBefore = git::commit_patch_cache().activity();
            cmd.consume();
        } else if (cmd.is("reading_checkpoint")) {
            checkpointPending = false;
            if (probe.label.empty()) { cmd.fail("No reading probe armed"); return; }
            if (!probe.readyMs) {
                if (Clock::now() - probe.armed > std::chrono::seconds(15)) {
                    cmd.fail("Reading destination did not render: " + probe.label);
                    probe = {};
                } else {
                    checkpointPending = true;
                    cmd.frames_alive = 0;
                    cmd.retry();
                }
                return;
            }
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            auto* detail = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>();
            if (!repo) { cmd.fail("No repository at reading checkpoint"); return; }
            size_t bytes = repo->fullFileBytes.capacity() + repo->fullFileDecodedText.capacity() + 2 +
                owned_content_bytes(repo->fullFileDiff) + owned_content_bytes(repo->currentDiff) + owned_content_bytes(repo->stagedDiff);
            if (detail) bytes += owned_content_bytes(detail->commitDetailDiff) + detail->commitDetailBody.capacity() + 1;
            auto blob = git::blob_page_cache().activity();
            auto patch = git::commit_patch_cache().activity();
            nlohmann::json snapshot{{"schema_version", 1}, {"label", probe.label},
                {"repository", repo->repoPath}, {"kind", probe.kind}, {"path", probe.path}, {"revision", probe.revision},
                {"selected_commit", repo->selectedCommitHash()}, {"source_path", repo->fullFilePath()},
                {"source_revision", repo->fullFileRevision()}, {"diff_target", repo->diffTargetFile()},
                {"selection_ms", *probe.selectionMs}, {"ready_ms", *probe.readyMs},
                {"owned_content_bytes", bytes},
                {"blob_cache", {{"bytes", git::blob_page_cache().bytes()}, {"hits", blob.first}, {"misses", blob.second},
                    {"hit_delta", blob.first - probe.blobBefore.first}, {"miss_delta", blob.second - probe.blobBefore.second}}},
                {"patch_cache", {{"bytes", git::commit_patch_cache().bytes()}, {"hits", patch.first}, {"misses", patch.second},
                    {"hit_delta", patch.first - probe.patchBefore.first}, {"miss_delta", patch.second - probe.patchBefore.second}}}};
            try {
                std::filesystem::create_directories(directory);
                std::ofstream output(directory / (probe.label + ".reading.json"));
                output.exceptions(std::ios::failbit | std::ios::badbit);
                output << snapshot.dump(2) << '\n';
                probe = {};
                cmd.consume();
            } catch (const std::exception& error) { cmd.fail(error.what()); }
        }
    }
};

void register_handlers(afterhours::SystemManager& manager, std::filesystem::path directory) {
    manager.register_update_system(std::make_unique<Handle>(std::move(directory)));
}

}

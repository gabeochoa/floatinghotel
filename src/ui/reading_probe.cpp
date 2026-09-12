#include "reading_probe.h"

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

bool checkpoint_pending() { return checkpointPending; }

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
            !detail->infoFuture.valid() && !detail->commitDetailDiff.empty() && detail->commitDetailError.empty();
    if (ready) probe.readyMs = elapsed;
}

struct Handle : afterhours::System<afterhours::testing::PendingE2ECommand> {
    std::filesystem::path directory;
    explicit Handle(std::filesystem::path path) : directory(std::move(path)) {}

    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed()) return;
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
            for (const auto& tab : repo->workspace().documents()) {
                nlohmann::json value{{"id", tab.id.value}};
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
                tabs.push_back(std::move(value));
            }
            try {
                std::filesystem::create_directories(directory);
                std::ofstream output(directory / (cmd.arg(1) + ".workspace.json"));
                output.exceptions(std::ios::failbit | std::ios::badbit);
                output << nlohmann::json{{"active", repo->workspace().active_id().value},
                    {"tabs", tabs}, {"inactive_payloads_empty", true}}.dump(2) << '\n';
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

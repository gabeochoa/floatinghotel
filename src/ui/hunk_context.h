#pragma once

#include "../git/hunk_context.h"
#include "../util/navigation.h"
#include "diff_metrics.h"

namespace ui::hunk_context {

inline std::string key(const ecs::FileDiff& file, const ecs::DiffHunk& hunk, bool above) {
    return diff_metrics().hunk_key(file, hunk) + (above ? "\nabove" : "\nbelow");
}

inline int requested(const ecs::RepoComponent& repo, const std::string& key) {
    const auto& counts = repo.workspace().document(repo.workspace().active_id())->contextLines;
    auto found = counts.find(key);
    return found == counts.end() ? 0 : found->second;
}


inline void poll(ecs::RepoComponent& repo, bool codeFocused) {
    auto& runtime = repo.hunkContext;
    auto& slot = runtime.slot;
    if (slot.phase == async_slot::Phase::Failed) {
        // Cache an exception failure once so content() renders the error
        // instead of relaunching for the same version every frame.
        if (!slot.key.identity.empty()) {
            auto found = runtime.entries.find(slot.key.identity);
            if (found == runtime.entries.end() || found->second.version != slot.key.version)
                runtime.entries[slot.key.identity] = {slot.key.version, {{}, slot.error}};
        }
        return;
    }
    auto landed = slot.poll(repo);
    if (!landed) {
        if (slot.phase == async_slot::Phase::Failed && !slot.key.identity.empty())
            runtime.entries[slot.key.identity] = {slot.key.version, {{}, slot.error}};
        return;
    }
    auto result = std::move(*landed);
    const auto slotKey = slot.key;
    size_t bytes = result.bytes, lines = result.lines.lines.size();
    for (const auto& [entryKey, entry] : runtime.entries) if (entryKey != slotKey.identity) {
        bytes += entry.result.bytes;
        lines += entry.result.lines.lines.size();
    }
    if (bytes > 256 * 1024 || lines > 4096) result = {{}, "Expanded context is limited to 256 KiB and 4,096 lines; open source to read more"};
    const bool failed = !result.error.empty();
    runtime.entries[slotKey.identity] = {slotKey.version, result};
    if (failed) slot.fail(result.error);
    navigation::restore_anchor(repo);
    if (codeFocused) navigation::focus_document(repo, reading::focus::Region::Code);
}

inline const ecs::HunkContextResult* content(ecs::RepoComponent& repo, const ecs::FileDiff& file,
                                           size_t index, bool above, const std::string& scope,
                                           int previousBelow = 0) {
    const auto identity = key(file, file.hunks[index], above);
    const int count = requested(repo, identity);
    if (!count) return nullptr;
    const auto wanted = git::context_range(file, index, above, count, previousBelow);
    const auto source = std::to_string(file.renderIdentity) + ":" + std::to_string(repo.dataGeneration) + ":";
    const auto version = source + std::to_string(wanted.oldLine) + ":" + std::to_string(wanted.newLine) + ":" + std::to_string(wanted.count);
    auto& runtime = repo.hunkContext;
    auto found = runtime.entries.find(identity);
    if (found != runtime.entries.end() && found->second.version == version) return &found->second.result;
    // Failed for this exact version is cached above as an entry; any
    // other Failed slot may be superseded by a new request below.
    if (!runtime.slot.busy() &&
        !(runtime.slot.phase == async_slot::Phase::Failed && runtime.slot.key.identity == identity &&
          runtime.slot.key.version == version)) {
        auto [oldRevision, newRevision] = diff_revisions(scope);
        git::FileRequest before{repo.repoPath, file.oldPath.empty() ? file.filePath : file.oldPath, oldRevision};
        git::FileRequest after{repo.repoPath, file.filePath, newRevision};
        runtime.slot.request({identity, version}, navigation::stamp(repo, version),
            async_work::launch([before, after, wanted](std::stop_token stop) {
                return git::read_hunk_context(before, after, wanted, stop);
            }, async_work::Priority::Background, ecs::HunkContextResult{{}, "Context reader is busy; try again"}));
    }
    if (found == runtime.entries.end() || !found->second.version.starts_with(source)) return nullptr;
    const auto& lines = found->second.result.lines;
    return lines.oldStart >= wanted.oldLine && lines.oldStart + lines.oldCount <= wanted.oldLine + wanted.count
        ? &found->second.result : nullptr;
}

}

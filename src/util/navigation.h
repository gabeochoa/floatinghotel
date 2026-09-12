#pragma once

#include "../ecs/components.h"

struct navigation {

    static reading::RequestStamp stamp(const ecs::RepoComponent& repo, std::string key) {
        return {repo.repoPath, repo.workspace_.location(), std::move(key), repo.workspace_.generation(), repo.dataGeneration};
    }

    static bool accepts(const ecs::RepoComponent& repo, const reading::RequestStamp& request, const std::string& key) {
        return request == stamp(repo, key);
    }

    static bool resolve_source(ecs::RepoComponent& repo, const reading::RequestStamp& request, const std::string& oid) {
        return accepts(repo, request, request.key) && repo.workspace_.resolve_source(request.generation, oid);
    }

    static bool resolve_review(ecs::RepoComponent& repo, const reading::RequestStamp& request,
                               const std::string& commit, const std::string& parent) {
        return accepts(repo, request, request.key) && repo.workspace_.resolve_review(request.generation, commit, parent);
    }

    static void remember_review_files(ecs::RepoComponent& repo, const std::vector<ecs::FileDiff>& files) {
        auto& summaries = repo.workspace_.current().files.emplace();
        summaries.reserve(files.size());
        for (const auto& file : files) {
            reading::FileSummary summary{file.filePath, file.additions, file.deletions, file.oldPath,
                ecs::file_change(file), ecs::diff_signature(file)};
            summary.requiresFileRecord = file.oldMode != file.newMode || !file.oldPath.empty();
            for (const auto& hunk : file.hunks)
                summary.hunkKeys.push_back(ecs::ReviewComponent::hunk_key(file.filePath, hunk));
            summaries.push_back(std::move(summary));
        }
    }

    static void release_inactive_review(const ecs::RepoComponent& repo, ecs::CommitDetailCache& cache) {
        if (repo.workspace_.active() == reading::Slot::Source || repo.selectedCommitHash() != cache.cachedCommitHash ||
            ecs::selected_commit_parent(repo) != cache.cachedParentHash || repo.repoPath != cache.cachedRepoPath)
            static_cast<ecs::CommitDetailRuntime&>(cache) = {};
    }

    static void release_source(ecs::RepoComponent& repo) {
        repo.fullFileFuture = {};
        repo.fullFileCacheKey.clear();
        repo.fullFileSourceKey.clear();
        std::vector<ecs::FileDiff>{}.swap(repo.fullFileDiff);
        std::string{}.swap(repo.fullFileBytes);
        std::string{}.swap(repo.fullFileDecodedText);
        repo.fullFileHexPreview = {};
        repo.fullFileHexPreviewKey.clear();
        repo.fullFileMarkdownCache = {};
        repo.fullFileError.clear();
        repo.blameFuture = {};
        repo.blameLine = {};
        repo.blameOpen = false;
    }

    static void finish(ecs::RepoComponent& repo, const reading::Location& before, bool changed) {
        if (!repo.navigationEffect) repo.navigationEffect.emplace();
        auto& effect = *repo.navigationEffect;
        effect.changed |= changed;
        effect.dismissedPanel |= repo.repoSearchOpen || repo.fileHistoryOpen || repo.commitSearchOpen;
        repo.repoSearchOpen = repo.fileHistoryOpen = repo.commitSearchOpen = false;
        if (!changed) return;
        repo.comparisonEditorOpen = false;
        effect.reviewing = repo.workspace_.history()[repo.workspace_.history_index()].reviewing;
        const auto after = repo.workspace_.location();
        const auto* source = std::get_if<reading::SourceLocation>(&after);
        const auto* oldSource = std::get_if<reading::SourceLocation>(&before);
        if (!reading::same_document(before, after)) release_source(repo);
        repo.originFileSummaries.clear();
        if (source) {
            const auto* origin = repo.workspace_.document(repo.workspace_.review());
            if (origin && origin->files) {
                for (const auto& file : *origin->files) {
                    ecs::FileDiff summary;
                    summary.filePath = file.path;
                    summary.additions = file.additions;
                    summary.deletions = file.deletions;
                    summary.oldPath = file.oldPath;
                    summary.isNew = file.change == 'A';
                    summary.isDeleted = file.change == 'D';
                    summary.isRenamed = file.change == 'R';
                    repo.originFileSummaries.push_back(std::move(summary));
                }
            }
            repo.comparisonFuture = {};
            std::vector<ecs::FileDiff>{}.swap(repo.comparisonDiff);
            repo.comparisonLoadedScope.clear();
        }
        if (repo.fullFileFuture.valid()) {
            repo.fullFileFuture = {};
            repo.fullFileCacheKey.clear();
        }
        if (source) {
            if (!oldSource || oldSource->destination != source->destination) {
                repo.fullFileNavigateFrames = source->line > 0 ? 3 : 0;
            } else if (oldSource->line != source->line) {
                repo.fullFileNavigateFrames = source->line > 0 ? 3 : 0;
            }
        } else {
            repo.diffTargetFrames = repo.diffTargetFile().empty() ? 0 : 4;
            repo.cachedFilePath.clear();
        }
        repo.rangeDiff.enabled = false;
        repo.rangeDiff.future = {};
        std::vector<ecs::FileDiff>{}.swap(repo.rangeDiff.display);
        if (repo.comparisonScope() != repo.comparisonLoadedScope) {
            repo.comparisonFuture = {};
            std::vector<ecs::FileDiff>{}.swap(repo.comparisonDiff);
            repo.comparisonLoadedScope.clear();
            repo.comparisonError.clear();
            repo.comparisonNeedsLoad = !repo.comparisonScope().empty();
            if (repo.comparisonNeedsLoad) {
                auto [base, target] = diff_revisions(repo.comparisonScope());
                repo.comparisonBase = std::move(base);
                repo.comparisonTarget = std::move(target);
            }
        }
    }

    static void open(ecs::RepoComponent& repo, reading::Location location, std::optional<bool> reviewing = {}) {
        auto before = repo.workspace_.location();
        if (auto* source = std::get_if<reading::SourceLocation>(&location); source && !source->origin)
            source->origin = repo.workspace_.review();
        bool mode = reviewing.value_or(repo.workspace_.history()[repo.workspace_.history_index()].reviewing);
        bool changed = repo.workspace_.open(std::move(location), mode);
        finish(repo, before, changed);
    }

    static void activate(ecs::RepoComponent& repo, reading::Slot slot) {
        if (slot == reading::Slot::Source) {
            if (repo.workspace_.source()) open(repo, *repo.workspace_.source());
        } else open(repo, repo.workspace_.review());
    }

    static void activate(ecs::RepoComponent& repo, reading::DocumentId id) {
        if (const auto* document = repo.workspace_.document(id)) open(repo, document->location);
    }

    static void close_source(ecs::RepoComponent& repo) {
        auto before = repo.workspace_.location();
        bool changed = repo.workspace_.close_source(repo.workspace_.history()[repo.workspace_.history_index()].reviewing);
        finish(repo, before, changed);
        ecs::cancel_hidden_file_read(repo);
    }

    static void step(ecs::RepoComponent& repo, int direction) {
        auto before = repo.workspace_.location();
        if (repo.workspace_.step(direction)) finish(repo, before, true);
    }

    static void return_to_review(ecs::RepoComponent& repo) {
        const auto& source = repo.workspace_.source();
        open(repo, source && source->origin ? *source->origin : repo.workspace_.review());
    }

    static void comparison_editor(ecs::RepoComponent& repo) {
        activate(repo, reading::Slot::Review);
        repo.comparisonEditorOpen = true;
    }

    static void reset(ecs::RepoComponent& repo) {
        auto before = repo.workspace_.location();
        repo.workspace_.reset();
        repo.reading = {};
        repo.fullFileFuture = {};
        repo.fullFileCacheKey.clear();
        repo.fullFileSourceKey.clear();
        finish(repo, before, true);
    }

    static std::string comparison_request_key(const ecs::RepoComponent& repo) {
        auto identity = repo.comparisonRequest == ecs::RepoComponent::ComparisonRequest::SubmittedForm
            ? repo.comparisonBase + "\n" + repo.comparisonTarget + "\n" + std::to_string(repo.comparisonMergeBase)
            : repo.comparisonScope();
        return identity + "\n" + std::to_string(repo.diffContext) + ":" + std::to_string(repo.ignoreWhitespace);
    }

    static bool accepts_comparison(ecs::RepoComponent& repo, const reading::RequestStamp& request) {
        if (accepts(repo, request, comparison_request_key(repo))) return true;
        if (repo.comparisonContext != repo.diffContext || repo.comparisonIgnoreWhitespace != repo.ignoreWhitespace)
            repo.comparisonNeedsLoad = !repo.comparisonScope().empty();
        return false;
    }

    static bool complete_comparison(ecs::RepoComponent& repo, const reading::RequestStamp& request,
                                    const std::string& base, const std::string& target) {
        if (!accepts_comparison(repo, request)) return false;
        if (repo.comparisonEditorOpen || !repo.workspace_.resolve_comparison(request.generation, base, target))
            open(repo, reading::review("compare:" + base + ":" + target));
        repo.comparisonLoadedScope = repo.comparisonScope();
        repo.comparisonNeedsLoad = false;
        return true;
    }

    static void clear_source_reveal(ecs::RepoComponent& repo) {
        repo.workspace_.clear_source_reveal();
    }

    static void restore_draft(ecs::RepoComponent& repo, const ecs::ReviewComponent& review) {
        if (!review.composingKey.empty()) open(repo, reading::review(review.composingScope, review.composingFile));
        else if (review.editingComment >= 0 && static_cast<size_t>(review.editingComment) < review.comments.size()) {
            const auto& comment = review.comments[review.editingComment];
            open(repo, reading::review(comment.scope, comment.file));
        }
    }

};

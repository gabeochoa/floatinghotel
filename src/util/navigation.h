#pragma once

#include "../ecs/components.h"
#include "../settings.h"

#include "reading_session.h"
#include "document_cycle.h"
#include "source_destination.h"

struct navigation {

    static void set_selection(ecs::RepoComponent& repo, std::optional<reading::CodeSelection> selection) {
        auto& current = repo.workspace_.current();
        if (current.selection != selection) repo.selectionCopy = {};
        current.selection = std::move(selection);
    }

    static bool toggle_feedback_line(ecs::RepoComponent& repo, reading::CodePosition point) {
        auto& document = repo.workspace_.current();
        if (document.feedbackGeneration != repo.dataGeneration || (!document.feedbackLines.empty() && document.feedbackLines.front().path != point.path)) document.feedbackLines.clear();
        document.feedbackGeneration = repo.dataGeneration;
        point.column = 1;
        auto found = std::find(document.feedbackLines.begin(), document.feedbackLines.end(), point);
        if (found != document.feedbackLines.end()) document.feedbackLines.erase(found);
        else {
            if (document.feedbackLines.size() >= 512) return false;
            document.feedbackLines.push_back(std::move(point));
        }
        return true;
    }

    static void clear_feedback_lines(ecs::RepoComponent& repo) { repo.workspace_.current().feedbackLines.clear(); }

    static void set_caret(ecs::RepoComponent& repo, reading::CodePosition position, bool focus = false) {
        if (repo.workspace_.active_source()) repo.workspace_.current().sourceFolds.reveal(position.line);
        repo.workspace_.current().caret = std::move(position);
        if (focus) focus_document(repo, reading::focus::Region::Code);
    }

    static void reveal_caret(ecs::RepoComponent& repo, reading::CodePosition position, float fraction) {
        set_caret(repo, position);
        auto& document = repo.workspace_.current();
        document.anchor = reading::ReadingAnchor{position.path, reading::anchor_revision(document.location),
            position.side, position.line, position.column, fraction, position.side == reading::DiffSide::Before ? '-' : ' '};
        restore_anchor(repo);
        if (auto* source = std::get_if<reading::SourceLocation>(&document.location);
            source && !repo.fullFilePage.contains(position.line, position.column, 0)) {
            source->line = position.line;
            source->column = position.column;
        }
    }

    static void return_to_feedback(ecs::RepoComponent& repo, const ecs::ReviewComponent::Comment& comment) {
        if (reading::scope(repo.workspace_.review()) != comment.scope) return;
        focus_document(repo, reading::focus::Region::Code);
        if (repo.workspace_.active() == reading::Slot::Source || comment.line <= 0 ||
            (!repo.workspace_.review().file.empty() && repo.workspace_.review().file != comment.file)) return;
        const auto side = comment.oldSide ? reading::DiffSide::Before : reading::DiffSide::After;
        auto point = repo.workspace_.current().caret.value_or(reading::CodePosition{});
        if (point.path != comment.file || point.side != side || point.line < comment.line ||
            point.line > std::max(comment.line, comment.endLine)) point = {comment.file, side, comment.line, 1};
        reveal_caret(repo, point, .15f);
    }

    static void request_caret_page(ecs::RepoComponent& repo, ecs::FilePageRequest request,
                                   reading::CodePosition position, std::optional<reading::CodeMotion> motion = {}) {
        cancel_anchor(repo);
        request.sourceIdentity = repo.fullFilePage.sourceIdentity;
        repo.fullFilePageRequest = std::move(request);
        repo.fullFileRequestedTargetLine = repo.fullFileRequestedTargetColumn = 0;
        repo.fullFileCacheKey.clear();
        repo.pendingCaret = std::move(position);
        repo.pendingCaretMotion = motion;
    }

    static std::string reading_layout_key(const ecs::RepoComponent& repo) {
        return "reading-layout:" + std::to_string(repo.workspace_.document(repo.workspace_.active_id())->sourceFolds.generation);
    }

    static void sync_source_folds(ecs::RepoComponent& repo, const std::string& identity) {
        repo.workspace_.current().sourceFolds.sync(identity);
    }

    static void reveal_source_line(ecs::RepoComponent& repo, int line) {
        repo.workspace_.current().sourceFolds.reveal(line);
    }

    static void toggle_source_fold(ecs::RepoComponent& repo, source_folding::Range range) {
        auto& document = repo.workspace_.current();
        if (!repo.workspace_.active_source() || !document.sourceFolds.toggle(range)) return;
        if (document.sourceFolds.closed(range)) {
            if (document.anchor && range.contains(document.anchor->line)) { document.anchor->line = range.first; document.anchor->column = 1; }
            if (document.caret && range.contains(document.caret->line)) { document.caret->line = range.first; document.caret->column = 1; }
        }
        restore_anchor(repo);
        focus_document(repo, reading::focus::Region::Code);
    }

    static void unfold_source(ecs::RepoComponent& repo) {
        repo.workspace_.current().sourceFolds.unfold();
        restore_anchor(repo);
        focus_document(repo, reading::focus::Region::Code);
    }

    static void set_review_display_mode(ecs::RepoComponent& repo, review_files::DisplayMode mode) {
        if (Settings::get().get_review_display_mode(repo.repoPath) == mode) return;
        Settings::get().set_review_display_mode(repo.repoPath, mode);
        restore_anchor(repo);
    }

    static constexpr int kContextStep = 20;
    static constexpr int kContextAll = 4096;

    static void expand_hunk_context(ecs::RepoComponent& repo, const std::string& key, int amount = kContextStep) {
        auto& counts = repo.workspace_.current().contextLines;
        if (!counts.contains(key) && counts.size() >= 256) return;
        counts[key] = std::min(kContextAll, counts[key] + amount);
        repo.hunkContext.future = {};
    }

    // One click reveals the whole folded gap; git::context_range clamps the
    // request to the lines actually between the neighbouring hunks.
    static void expand_hunk_context_all(ecs::RepoComponent& repo, const std::string& key) {
        auto& counts = repo.workspace_.current().contextLines;
        if (!counts.contains(key) && counts.size() >= 256) return;
        counts[key] = kContextAll;
        repo.hunkContext.future = {};
    }

    static bool follow_rewritten_tip(ecs::RepoComponent& repo, const std::string& from, const std::string& to) {
        if (!repo.workspace_.retarget_commit(from, to)) return false;
        repo.hunkContext = {};
        return true;
    }

    static void toggle_commit_details(ecs::RepoComponent& repo) {
        auto& document = repo.workspace_.current();
        document.detailsExpanded = !document.detailsExpanded;
    }

    static reading::FindState& find(ecs::RepoComponent& repo) { return repo.workspace_.current().find; }

    static void focus_document(ecs::RepoComponent& repo, reading::focus::Region region = reading::focus::Region::DocumentTabs) {
        const auto id = repo.workspace_.active_id();
        repo.readingFocus = reading::focus::Target{repo.repoPath, id, region, region == reading::focus::Region::Code ? "viewport" : "",
            region == reading::focus::Region::DocumentTabs ? "content_document_" + std::to_string(id.value) : ""};
    }

    static void open_find(ecs::RepoComponent& repo, std::string seed = {}, std::optional<reading::ReadingAnchor> position = {}) {
        auto& state = find(repo);
        state.open = state.focus = true;
        state.navigate = false;
        state.pendingStep = 0;
        if (!seed.empty()) {
            state.query = std::move(seed);
            state.index = 0;
            state.position = std::move(position);
        }
    }

    static void close_find(ecs::RepoComponent& repo) {
        if (repo.sourceFind.future.valid()) repo.sourceFind = {};
        auto& state = find(repo);
        state.open = state.focus = state.navigate = false;
        state.pendingStep = 0;
        focus_document(repo, reading::focus::Region::Code);
    }


    static reading::RequestStamp stamp(const ecs::RepoComponent& repo, std::string key) {
        return {repo.repoPath, repo.workspace_.location(), std::move(key), repo.workspace_.generation(), repo.dataGeneration};
    }

    static void open_catalog(ecs::RepoComponent& repo, ecs::LayoutComponent& layout, git::catalog::Kind category) {
        layout.filePickerScope = {};
        layout.filePickerScope.owner = stamp(repo, {});
        layout.filePickerScope.documentRevision = reading::source_revision_for(repo.workspace().location());
        layout.filePickerScope.category = category;
        layout.filePickerQuery.clear();
        layout.filePickerCacheKey.clear();
        layout.filePickerPosition = {};
        layout.filePickerOpen = layout.filePickerFocus = true;
    }

    static bool accepts(const ecs::RepoComponent& repo, const reading::RequestStamp& request, const std::string& key) {
        return request.repository == repo.repoPath && reading::same_document(request.document, repo.workspace_.location()) &&
            request.key == key && request.generation == repo.workspace_.generation() && request.dataGeneration == repo.dataGeneration;
    }

    static bool resolve_source(ecs::RepoComponent& repo, const reading::RequestStamp& request, const std::string& oid) {
        return accepts(repo, request, request.key) && repo.workspace_.resolve_source(request.generation, oid);
    }

    static bool resolve_review(ecs::RepoComponent& repo, const reading::RequestStamp& request,
                               const std::string& commit, const std::string& parent) {
        return accepts(repo, request, request.key) && repo.workspace_.resolve_review(request.generation, commit, parent);
    }

    static void remember_document_subject(ecs::RepoComponent& repo, std::string subject) {
        repo.workspace_.current().subject = std::move(subject);
    }

    static void remember_commit_subject(ecs::RepoComponent& repo, std::string subject) {
        auto& document = repo.workspace_.current();
        const auto* review = std::get_if<reading::ReviewLocation>(&document.location);
        if (review && std::holds_alternative<reading::CommitReview>(review->destination))
            document.subject = std::move(subject);
    }

    static void remember_review_files(ecs::RepoComponent& repo, const std::vector<ecs::FileDiff>& files) {
        auto& summaries = repo.workspace_.current().files.emplace();
        summaries.reserve(files.size());
        for (const auto& file : files) {
            reading::FileSummary summary{file.filePath, file.additions, file.deletions, file.oldPath,
                ecs::file_change(file), ecs::diff_signature(file)};
            summary.partial = file.isPartialContent;
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
        repo.sourceFoldIdentity = 0;
        std::vector<source_folding::Range>{}.swap(repo.sourceFoldRanges);
        repo.diffSyntax = {};
        repo.sourceFind = {};
        repo.selectionCopy = {};
        repo.pendingCaret.reset();
        repo.pendingCaretMotion.reset();
        repo.fullFileFuture = {};
        repo.fullFileCacheKey.clear();
        repo.fullFileSourceKey.clear();
        std::vector<ecs::FileDiff>{}.swap(repo.fullFileDiff);
        repo.sourceWindow = {};
        repo.fullFileExtendRequest = false;
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
        effect.focus = reading::FocusPolicy::Document;
        effect.changed |= changed;
        effect.dismissedPanel |= repo.fileHistoryOpen || repo.commitSearchOpen;
        repo.fileHistoryOpen = repo.commitSearchOpen = false;
        if (!changed) return;
        repo.comparisonEditorOpen = false;
        effect.reviewing = repo.workspace_.history()[repo.workspace_.history_index()].reviewing;
        const auto after = repo.workspace_.location();
        const auto* source = std::get_if<reading::SourceLocation>(&after);
        const auto* oldSource = std::get_if<reading::SourceLocation>(&before);
        if (source && repo.workspace_.current().restoreAnchor && repo.workspace_.current().anchor)
            repo.workspace_.current().sourceFolds.reveal(repo.workspace_.current().anchor->line);
        if (!reading::same_document(before, after)) release_source(repo);
        if (!reading::same_document(before, after) || repo.workspace_.current().contextLines.empty()) repo.hunkContext = {};
        repo.originFileSummaries.clear();
        if (source) {
            const auto* origin = repo.workspace_.retained_review();
            if (origin && origin->files) {
                for (const auto& file : *origin->files) {
                    ecs::FileDiff summary;
                    summary.filePath = file.path;
                    summary.additions = file.additions;
                    summary.deletions = file.deletions;
                    summary.oldPath = file.oldPath;
                    summary.isPartialContent = file.partial;
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
            } else if (oldSource->line != source->line || oldSource->column != source->column) {
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

    static void open(ecs::RepoComponent& repo, reading::Location location, std::optional<bool> reviewing = {},
                     reading::OpenMode mode = reading::OpenMode::Preview, std::optional<reading::ReadingAnchor> anchor = {}) {
        repo.workspace_.lastClick_.reset();
        auto before = repo.workspace_.location();
        if (!anchor && reading::same_document(before, location)) {
            if (const auto* source = std::get_if<reading::SourceLocation>(&location); source && source->line == 0)
                location = before;
            else if (const auto* review = std::get_if<reading::ReviewLocation>(&location); review && review->file.empty())
                location = before;
        }
        if (auto* source = std::get_if<reading::SourceLocation>(&location)) {
            if (!source->origin) source->origin = repo.workspace_.review();
            if (!source->originAnchor) {
                if (repo.workspace_.active() == reading::Slot::Review && reading::same_document(before, *source->origin))
                    source->originAnchor = repo.workspace_.current().anchor;
                else if (const auto* previous = std::get_if<reading::SourceLocation>(&before);
                    previous && previous->origin && reading::same_document(*previous->origin, *source->origin))
                    source->originAnchor = previous->originAnchor;
            }
        }
        bool reviewingMode = reviewing.value_or(repo.workspace_.history()[repo.workspace_.history_index()].reviewing);
        bool changed = repo.workspace_.open(std::move(location), reviewingMode, mode, anchor);
        if (const auto* source = repo.workspace_.active_source(); changed && source && source->line > 0) {
            set_selection(repo, {});
            set_caret(repo, {source->destination.path, reading::DiffSide::After, source->line, std::max(1, source->column)});
        }
        if (repo.workspace_.current().subject.empty())
            for (const auto* entries : {&repo.commitLog, &repo.fileHistoryEntries, &repo.commitSearchEntries})
                for (const auto& entry : *entries)
                    if (entry.hash == repo.selectedCommitHash()) remember_commit_subject(repo, entry.subject);
        finish(repo, before, changed);
        if (anchor) repo.fullFileNavigateFrames = repo.diffTargetFrames = 0;
    }

    static void open_source(ecs::RepoComponent& repo, const ecs::FileDiff& file,
                            std::optional<reading::ReadingAnchor> point = {}) {
        open(repo, reading::source_at_diff(repo.workspace_.review(), file, std::move(point)));
    }

    static void go_to_review_line(ecs::RepoComponent& repo, ecs::ReviewComponent& review,
                                  const ecs::FileDiff& file, reading::ReadingAnchor anchor) {
        auto location = repo.workspace_.review();
        location.file = file.filePath;
        const auto scope = reading::scope(location);
        review.foldedFiles.erase(scope + "\n" + file.filePath);
        for (const auto& hunk : file.hunks) {
            const int start = anchor.side == reading::DiffSide::Before ? hunk.oldStart : hunk.newStart;
            const int count = anchor.side == reading::DiffSide::Before ? hunk.oldCount : hunk.newCount;
            if (anchor.line >= start && anchor.line - start < count)
                review.foldedHunks.erase(scope + "\n" + ecs::ReviewComponent::hunk_key(file.filePath, hunk));
        }
        open(repo, location, {}, reading::OpenMode::Keep, anchor);
        set_selection(repo, {});
        set_caret(repo, {anchor.path, anchor.side, anchor.line, anchor.column});
    }

    static void go_to_review_change(ecs::RepoComponent& repo, ecs::ReviewComponent& review,
                                    const ecs::FileDiff& file, std::optional<reading::ReadingAnchor> anchor, int cursor) {
        if (anchor) go_to_review_line(repo, review, file, *anchor);
        else open(repo, reading::review(reading::scope(repo.workspace_.review()), file.filePath));
        review.cursor = cursor;
        review.cursorMoved = false;
        review.cursorApprove = review.cursorComment = false;
        repo.navigationEffect->focus = reading::FocusPolicy::Caller;
        focus_document(repo, reading::focus::Region::Code);
    }

    static void restore_session(ecs::RepoComponent& repo, const reading::ReadingSession& session, bool reviewing) {
        if (session.documents.empty()) return;
        auto before = repo.workspace_.location();
        auto& workspace = repo.workspace_;
        const auto previousGeneration = workspace.generation_;
        workspace.reset();
        workspace.documents_.clear();
        workspace.nextId_ = 1;
        std::uint64_t mostRecent = 0;
        for (const auto& saved : session.documents) {
            reading::Document document{reading::DocumentId{workspace.nextId_++}, saved.location, saved.lastActivated, {}, false,
                saved.subject, saved.anchor, saved.anchor.has_value(), reading::unresolved_destination(saved.location)};
            if (auto* source = std::get_if<reading::SourceLocation>(&document.location); source && document.anchor) {
                source->line = document.anchor->line;
                source->column = document.anchor->column;
            }
            mostRecent = std::max(mostRecent, document.lastActivated);
            workspace.documents_.push_back(std::move(document));
        }
        workspace.active_ = workspace.documents_[std::min(session.active, workspace.documents_.size() - 1)].id;
        workspace.generation_ = std::max(previousGeneration, mostRecent) + 1;
        workspace.current().lastActivated = workspace.generation_;
        const auto recent = reading::recent_documents(workspace);
        for (auto id = recent.rbegin(); id != recent.rend(); ++id) workspace.remember_source(workspace.document(*id)->location);
        workspace.history_ = {{workspace.location(), reviewing}};
        workspace.index_ = 0;
        repo.reading = {};
        finish(repo, before, true);
    }

    static void resolve_saved_revision(ecs::RepoComponent& repo) {
        auto& document = repo.workspace_.current();
        if (!document.unresolvedSavedRevision) return;
        document.unresolvedSavedRevision = false;
        document.restoreAnchor = false;
        document.anchor.reset();
        ++repo.workspace_.generation_;
        finish(repo, document.location, true);
    }

    static void remember_anchor(ecs::RepoComponent& repo, reading::ReadingAnchor anchor) {
        auto& document = repo.workspace_.current();
        if (!document.restoreAnchor && anchor.revision == reading::anchor_revision(document.location)) {
            repo.workspace_.history_[repo.workspace_.index_].anchor = anchor;
            document.anchor = std::move(anchor);
        }
    }

    static void restore_anchor(ecs::RepoComponent& repo) {
        auto& document = repo.workspace_.current();
        if (!document.anchor) return;
        document.restoreAnchor = true;
        repo.workspace_.history_[repo.workspace_.index_].anchor = document.anchor;
        if (auto* source = std::get_if<reading::SourceLocation>(&document.location); source &&
            (repo.fullFileDiff.empty() || document.anchor->line < repo.fullFilePage.begin.line || document.anchor->line >= repo.fullFilePage.next.line)) {
            source->line = document.anchor->line;
            source->column = document.anchor->column;
        }
    }

    static void cancel_anchor(ecs::RepoComponent& repo) {
        auto& document = repo.workspace_.current();
        document.restoreAnchor = false;
        document.anchor.reset();
        repo.workspace_.history_[repo.workspace_.index_].anchor.reset();
        repo.fullFileNavigateFrames = repo.diffTargetFrames = 0;
        repo.workspace_.clear_source_reveal();
    }

    static void restored_anchor(ecs::RepoComponent& repo) {
        repo.workspace_.current().restoreAnchor = false;
        repo.fullFileNavigateFrames = repo.diffTargetFrames = 0;
        if (repo.workspace_.active() == reading::Slot::Source) repo.workspace_.clear_source_reveal();
    }

    static void keep(ecs::RepoComponent& repo, reading::DocumentId id) {
        repo.workspace_.keep(id);
    }

    static void preview(ecs::RepoComponent& repo, reading::Location location, std::optional<reading::ReadingAnchor> anchor = {}) {
        open(repo, std::move(location), {}, reading::OpenMode::Preview, std::move(anchor));
        repo.navigationEffect->focus = reading::FocusPolicy::Caller;
        repo.readingFocus.reset();
    }

    static void click(ecs::RepoComponent& repo, reading::Location location, bool enter = false,
                      reading::ClickRegion region = reading::ClickRegion::Tree, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) {
        auto& workspace = repo.workspace_;
        const bool repeated = workspace.lastClick_ && region == workspace.lastClickRegion_ &&
            (region == reading::ClickRegion::Tabs ? reading::same_document(*workspace.lastClick_, location) : *workspace.lastClick_ == location) &&
            now >= workspace.lastClickTime_ && now - workspace.lastClickTime_ <= std::chrono::milliseconds(500);
        const auto* existing = region == reading::ClickRegion::Tabs ? workspace.document(location) : nullptr;
        if (existing) {
            const auto id = existing->id;
            activate(repo, id);
            if (enter || repeated) keep(repo, id);
        } else open(repo, location, {}, enter || repeated ? reading::OpenMode::Keep : reading::OpenMode::Preview);
        if ((region == reading::ClickRegion::Tree || region == reading::ClickRegion::History) && repo.navigationEffect) {
            repo.navigationEffect->focus = reading::FocusPolicy::Caller;
            repo.readingFocus.reset();
        } else if (enter && region == reading::ClickRegion::Search) {
            focus_document(repo);
        }
        workspace.lastClick_ = std::move(location);
        workspace.lastClickRegion_ = region;
        workspace.lastClickTime_ = now;
    }

    static void activate(ecs::RepoComponent& repo, reading::Slot slot) {
        if (const auto* document = repo.workspace_.recent(slot)) activate(repo, document->id);
    }

    static void activate(ecs::RepoComponent& repo, reading::DocumentId id) {
        if (const auto* document = repo.workspace_.document(id)) {
            const bool changed = id != repo.workspace_.active_id();
            auto caret = document->caret;
            open(repo, document->location);
            repo.workspace_.current().caret = std::move(caret);
            if (changed) restore_anchor(repo);
        }
    }

    static bool reorder(ecs::RepoComponent& repo, reading::DocumentId id, size_t insertion) {
        return repo.workspace_.reorder(id, insertion);
    }

    static void close_source(ecs::RepoComponent& repo) {
        if (const auto* source = repo.workspace_.recent(reading::Slot::Source)) close(repo, source->id);
    }

    static void close(ecs::RepoComponent& repo, reading::DocumentId id) {
        auto before = repo.workspace_.location();
        bool changed = repo.workspace_.close(id, repo.workspace_.history()[repo.workspace_.history_index()].reviewing);
        finish(repo, before, changed);
        if (changed) restore_anchor(repo);
    }

    static void close_others(ecs::RepoComponent& repo, reading::DocumentId id, bool onlyRight = false) {
        if (!repo.workspace_.document(id)) return;
        std::vector<reading::DocumentId> closing;
        bool past = false;
        for (const auto& tab : repo.workspace_.documents()) {
            if (tab.id == id) { past = true; continue; }
            if (!onlyRight || past) closing.push_back(tab.id);
        }
        for (auto target : closing) close(repo, target);
    }

    static void reopen_closed(ecs::RepoComponent& repo) {
        auto before = repo.workspace_.location();
        bool changed = repo.workspace_.reopen(repo.workspace_.history()[repo.workspace_.history_index()].reviewing);
        finish(repo, before, changed);
        if (changed) restore_anchor(repo);
    }

    static void step(ecs::RepoComponent& repo, int direction) {
        repo.workspace_.lastClick_.reset();
        auto before = repo.workspace_.location();
        if (repo.workspace_.step(direction)) {
            if (const auto* source = repo.workspace_.active_source(); source && source->line > 0)
                set_caret(repo, {source->destination.path, reading::DiffSide::After, source->line, std::max(1, source->column)});
            finish(repo, before, true);
        }
    }

    static void return_to_review(ecs::RepoComponent& repo) {
        const auto* source = repo.workspace_.active_source();
        const auto origin = source && source->origin ? *source->origin : repo.workspace_.review();
        const auto anchor = source ? source->originAnchor : std::nullopt;
        open(repo, origin);
        if (anchor && anchor->revision == reading::anchor_revision(repo.workspace_.location())) {
            repo.workspace_.current().anchor = anchor;
            repo.workspace_.current().restoreAnchor = true;
            repo.workspace_.history_[repo.workspace_.index_].anchor = anchor;
        }
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

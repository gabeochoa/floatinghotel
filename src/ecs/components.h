#pragma once
#include <atomic>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../../vendor/afterhours/src/core/entity_helper.h"
#include "../git/git_runner.h"
#include "../git/history_query.h"
#include "../util/codeowners.h"
#include "../util/code_bookmark.h"
#include "../util/hex_view.h"
#include "../util/markdown_preview.h"
#include "../util/diff_revisions.h"
#include "../util/reading_workspace.h"
#include "../util/review_files.h"
#include "../util/review_comment_kind.h"
#include "../util/review_verdict.h"
#include "../util/async_task.h"
#include "../util/refresh_scope.h"

namespace ecs {

struct SearchMatch {
    std::string file;
    int line = 0;
    std::string text;
    std::string revision;
};

struct SearchMatching {
    bool regularExpression = false;
    bool caseSensitive = true;
    bool wholeWord = false;
};

struct SearchQuery {
    std::string repoPath;
    std::string revision;
    std::string text;
    bool changedOnly = false;
    std::vector<std::string> paths;
    std::vector<std::string> removedPaths;
    std::string beforeRevision;
    SearchMatching matching;
    std::string includeGlob;
    std::string excludeGlob;
};

struct SearchResult {
    std::string revision;
    std::vector<SearchMatch> matches;
    std::string error;
    bool truncated = false;
    size_t capturedBytes = 0;
};

struct SearchPreview {
    SearchMatch match;
    std::vector<std::pair<int, std::string>> lines;
    std::string error;
    bool changedSinceSearch = false;
};

struct BlameLine {
    std::string hash;
    std::string author;
    std::string summary;
    std::string file;
    std::string content;
    int originalLine = 0;
    int finalLine = 0;
};

// ---- Sub-structs (not components, just data) ----

struct FileStatus {
    std::string path;
    char indexStatus = ' ';    // Staged status character
    char workTreeStatus = ' '; // Worktree status character
    std::string origPath;      // For renames
    int additions = 0;
    int deletions = 0;
    bool isSubmodule = false;  // gitlink (porcelain v2 'sub' field starts 'S')
};

struct CommitEntry {
    std::string hash;          // Full 40-char hash
    std::string shortHash;     // 7-char abbreviated
    std::string subject;       // First line of commit message
    std::string author;
    std::string authorDate;    // ISO 8601 format
    std::string decorations;   // Branch/tag labels from %D
    std::string parentHashes;  // Space-separated parent hashes from %P
};

struct DiffHunk {
    int oldStart = 0, oldCount = 0;
    int newStart = 0, newCount = 0;
    std::string header;        // The @@ line
    std::vector<std::string> lines; // Lines with +/-/space prefix
    std::set<size_t> noNewline;
    std::set<size_t> movedLines;
};

struct CommitReviewQueue {
    std::vector<CommitEntry> commits;
    size_t position = 0;
    std::set<std::string> completed;
};

inline CommitReviewQueue refreshed_review_queue(const CommitReviewQueue& previous, std::vector<CommitEntry> commits) {
    CommitReviewQueue result{std::move(commits), 0, previous.completed};
    std::string current = previous.position < previous.commits.size() ? previous.commits[previous.position].hash : "";
    for (size_t i = 0; i < result.commits.size(); ++i)
        if (result.commits[i].hash == current) result.position = i;
    std::erase_if(result.completed, [&](const auto& hash) {
        return std::none_of(result.commits.begin(), result.commits.end(), [&](const auto& commit) { return commit.hash == hash; });
    });
    return result;
}

inline std::uint64_t next_render_identity() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

struct FileDiff {
    std::string filePath;
    std::string oldPath;       // For renames
    int additions = 0;
    int deletions = 0;
    bool isNew = false;
    bool isDeleted = false;
    bool isRenamed = false;
    bool isBinary = false;
    bool isFullContent = false;
    bool isSubmodule = false;  // gitlink change (index mode 160000)
    std::vector<DiffHunk> hunks;
    std::string oldMode;
    std::string newMode;
    std::string oldObject;
    std::string newObject;
    std::uint64_t renderIdentity = next_render_identity();
    bool isPartialContent = false;
};

struct FilePageCursor {
    uint64_t offset = 0;
    int line = 1;
    bool continuation = false;
};

struct FilePageRequest {
    enum class Action { Start, Next, Previous, TargetLine };
    Action action = Action::Start;
    FilePageCursor cursor;
    int targetLine = 0;
    std::string sourceIdentity;
};

struct FilePage {
    FilePageCursor begin;
    FilePageCursor next;
    uint64_t totalBytes = 0;
    std::string blob;
    std::string encoding;
    std::string sourceIdentity;
};

struct FullFileContent {
    FileDiff diff;
    std::string raw;
    std::string error;
    FilePage page;
    std::string encodingLabel;
    std::string decodedText;
    std::string resolvedRevision;
};

inline char file_change(const FileDiff& file) {
    return file.isRenamed ? 'R' : file.isDeleted ? 'D' : file.isNew ? 'A' : 'M';
}

inline std::vector<size_t> visible_file_indices(const std::vector<FileDiff>& files, const review_files::Filter& filter) {
    std::vector<size_t> indices;
    for (size_t i = 0; i < files.size(); ++i)
        if (files[i].isFullContent || review_files::matches(filter, files[i].filePath, file_change(files[i]))) indices.push_back(i);
    std::sort(indices.begin(), indices.end(), [&](size_t a, size_t b) {
        return review_files::precedes(filter.sort, files[a].filePath, files[a].additions + files[a].deletions,
            files[b].filePath, files[b].additions + files[b].deletions);
    });
    return indices;
}

struct CommitPatch {
    std::vector<FileDiff> files;
    std::string error;
    std::string resolvedCommit;
    std::string resolvedParent;
};

inline std::string hunk_signature(const DiffHunk& hunk) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto& line : hunk.lines) {
        for (unsigned char ch : line) {
            hash ^= ch;
            hash *= 1099511628211ull;
        }
        if (!hunk.noNewline.contains(static_cast<size_t>(&line - hunk.lines.data()))) {
            hash ^= '\n';
            hash *= 1099511628211ull;
        }
    }
    return hunk.header + ":" + std::to_string(hash);
}

struct BranchInfo {
    std::string name;
    std::string shortHash;
    bool isLocal = true;
    bool isCurrent = false;
    std::string upstream;
    std::string tracking; // e.g. "[ahead 3, behind 1]"
};

// ---- ECS Components ----

struct ReadingPositions {
    std::map<std::string, std::pair<float, float>> offsets;
    std::string key;
    int entity = -1;
    std::pair<float, float> lastOffset{};
    std::pair<float, float> restoringOffset{};
    int restoreFrames = 0;
};

struct RangeDiffState {
    bool enabled = false;
    std::string oldRange;
    std::string newRange;
    std::array<std::string, 4> revisions;
    std::array<std::string, 4> resolved;
    size_t next = 0;
    reading::RequestStamp requestStamp;
    async_work::Task<git::GitResult> future;
    std::vector<FileDiff> display;
    std::string error;
};

struct RepoComponent : public afterhours::BaseComponent {
    RangeDiffState rangeDiff;
    bool reviewWorkspace = false;
    review_files::Filter fileFilter;
    ReadingPositions reading;
private:
    reading::ReadingWorkspace workspace_;
    friend struct ::navigation;
public:
    const reading::ReadingWorkspace& workspace() const { return workspace_; }
    std::optional<reading::NavigationEffect> navigationEffect;
    std::vector<FileDiff> originFileSummaries;
    std::string repoPath;
    std::string currentBranch;
    bool isDirty = false;
    bool isDetachedHead = false;
    std::string headCommitHash;
    int aheadCount = 0;
    int behindCount = 0;

    std::vector<FileStatus> stagedFiles;
    std::vector<FileStatus> unstagedFiles;
    std::vector<std::string> untrackedFiles;
    std::vector<CommitEntry> commitLog;
    int commitLogLoaded = 0;
    bool commitLogHasMore = true;
    bool commitLogLoading = false; // a `git log` is in flight for this repo

    // Branch data (T031)
    std::vector<BranchInfo> branches;

    std::vector<FileDiff> currentDiff;
    std::vector<FileDiff> stagedDiff;

    std::string cachedFilePath;
    std::string untrackedDiffKey;
    std::optional<FileDiff> untrackedDiff;

    bool refreshRequested = false;
    refresh_scope::Scope refreshScope = refresh_scope::Scope::Full;
    std::string lastRefreshScope;
    bool ignoreWhitespace = false;
    int diffContext = 3;
    std::string fullFileCacheKey;
    std::string fullFileSourceKey;
    std::string fullFileHexPreviewKey;
    hex_view::Preview fullFileHexPreview;
    std::string fullFileDecodedText;
    markdown_preview::Cache fullFileMarkdownCache;
    bool fullFileMarkdownPreview = false;
    std::vector<FileDiff> fullFileDiff;
    std::string fullFileError;
    std::string fullFileBytes;
    async_work::Task<FullFileContent> fullFileFuture;
    reading::RequestStamp fullFileRequestStamp;
    FilePage fullFilePage;
    FilePageRequest fullFilePageRequest;
    std::string fullFileEncodingOverride = "auto";
    std::string fullFileEncodingLabel;
    int fullFileRequestedTargetLine = 0;
    bool isRefreshing = false;
    bool hasLoadedOnce = false;
    unsigned repoVersion = 0;
    unsigned dataGeneration = 0;
    unsigned patchGeneration = 0;
    std::vector<std::string> allFilePaths;
    std::string filesError;
    std::string codeownersKey;
    async_work::Task<codeowners::Document> codeownersFuture;
    codeowners::Document codeownersDocument;
    std::unordered_map<std::string, std::string> codeownersByPath;
    bool repoSearchOpen = false;
    bool repoSearchFocus = false;
    std::string repoSearchQuery;
    std::string repoSearchPath;
    std::string repoSearchError;
    bool repoSearchTruncated = false;
    size_t repoSearchCapturedBytes = 0;
    async_work::Task<SearchResult> repoSearchFuture;
    reading::RequestStamp repoSearchFutureStamp;
    std::string repoSearchRevision;
    bool repoSearchChangedOnly = false;
    SearchMatching repoSearchMatching;
    std::string repoSearchIncludeGlob;
    std::string repoSearchExcludeGlob;
    std::vector<SearchMatch> repoSearchResults;
    bool repoSearchPreviewOpen = false;
    async_work::Task<SearchPreview> repoSearchPreviewFuture;
    reading::RequestStamp repoSearchPreviewFutureStamp;
    SearchPreview repoSearchPreview;
    int fullFileNavigateFrames = 0;
    bool fileHistoryOpen = false;
    std::string fileHistoryPath;
    std::string fileHistoryRevision;
    std::string fileHistoryError;
    int fileHistoryLimit = 200;
    async_work::Task<git::GitResult> fileHistoryFuture;
    reading::RequestStamp fileHistoryFutureStamp;
    std::vector<CommitEntry> fileHistoryEntries;
    async_work::Task<git::GitResult> blameFuture;
    reading::RequestStamp blameFutureStamp;
    BlameLine blameLine;
    std::string blameError;
    bool blameOpen = false;
    bool commitSearchOpen = false;
    git::HistoryQuery commitSearchQuery;
    int commitSearchLimit = 200;
    async_work::Task<git::GitResult> commitSearchFuture;
    reading::RequestStamp commitSearchFutureStamp;
    std::vector<CommitEntry> commitSearchEntries;
    std::string commitSearchError;
    bool comparisonEditorOpen = false;
    bool comparisonNeedsLoad = false;
    std::string comparisonBase;
    std::string comparisonTarget;
    bool comparisonMergeBase = false;
    async_work::Task<git::RevisionComparison> comparisonFuture;
    std::vector<FileDiff> comparisonDiff;
    std::string comparisonLoadedScope;
    reading::RequestStamp comparisonRequestStamp;
    enum class ComparisonRequest { Document, SubmittedForm };
    ComparisonRequest comparisonRequest = ComparisonRequest::Document;
    std::string comparisonError;
    std::string reviewQueueScope;
    async_work::Task<git::GitResult> reviewQueueFuture;
    reading::RequestStamp reviewQueueFutureStamp;
    std::string reviewQueueError;
    int comparisonContext = 3;
    bool comparisonIgnoreWhitespace = false;
    int diffTargetFrames = 0;
    size_t bookmarkPage = 0;

    const std::string& selectedFilePath() const { return workspace_.review().file; }
    const std::string& diffTargetFile() const { return workspace_.review().file; }
    const std::string& selectedCommitHash() const {
        static const std::string empty;
        const auto* commit = std::get_if<reading::CommitReview>(&workspace_.review().destination);
        return commit ? reading::revision_text(commit->commit) : empty;
    }
    bool selectedFileStaged() const {
        const auto* changes = std::get_if<reading::WorkingChanges>(&workspace_.review().destination);
        return changes && changes->staged;
    }
    int fullFileTargetLine() const { return workspace_.source() ? workspace_.source()->line : 0; }
    const std::string& fullFilePath() const {
        static const std::string empty;
        return workspace_.source() ? workspace_.source()->destination.path : empty;
    }
    const std::string& fullFileRevision() const {
        static const std::string empty;
        return workspace_.source() ? reading::revision_text(workspace_.source()->destination.revision) : empty;
    }
    const std::string comparisonScope() const {
        return std::holds_alternative<reading::ComparisonReview>(workspace_.review().destination)
            ? reading::scope(workspace_.review()) : "";
    }
    bool comparisonOpen() const {
        return workspace_.active() == reading::Slot::Review && (comparisonEditorOpen || !comparisonScope().empty());
    }
};

inline bool source_tab_active(const RepoComponent& repo) {
    return repo.workspace().active() == reading::Slot::Source && !repo.fullFilePath().empty();
}

inline void cancel_hidden_file_read(RepoComponent& repo) {
    if (repo.fullFilePath().empty() && repo.fullFileFuture.valid()) {
        repo.fullFileFuture = {};
        repo.fullFileCacheKey.clear();
    }
}

inline std::string selected_commit_parent(const RepoComponent& repo) {
    const auto* commit = std::get_if<reading::CommitReview>(&repo.workspace().review().destination);
    return commit && commit->parent ? reading::revision_text(*commit->parent) : "";
}

inline std::string commit_review_scope(const RepoComponent& repo) {
    return reading::scope(repo.workspace().review());
}

struct CommitDetailRuntime {
    reading::RequestStamp requestStamp;
    std::string cachedCommitHash;
    std::string cachedParentHash;
    std::string cachedRepoPath;
    CommitEntry entry;
    async_work::Task<CommitPatch> patchFuture;
    int cachedContext = -1;
    bool cachedIgnoreWhitespace = false;
    async_work::Task<git::GitResult> infoFuture;
    std::vector<FileDiff> commitDetailDiff;
    std::string commitDetailBody;
    bool messageExpanded = false;
    float messageWrapWidth = 0.f;
    float messageFontSize = 0.f;
    std::vector<std::string> messageLines;
    size_t messageVisibleRows = 0;
    std::string commitDetailAuthorEmail;
    std::string commitDetailParents;
    std::string commitDetailError;
    bool fileOverviewExpanded = false;
};

struct CommitDetailCache : public afterhours::BaseComponent, CommitDetailRuntime {};

// Per-tab "Ballroom" review state (see docs/mocks/ballroom.html).
struct ReviewComponent : public afterhours::BaseComponent {
    struct Comment {
        std::string scope;  // "wt" for working tree, or a commit SHA
        std::string file;
        int line = 0;
        std::string text;
        int endLine = 0;
        bool oldSide = false;
        bool resolved = false;
        std::string revision;
        std::string codeContext;
        ReviewCommentKind kind = ReviewCommentKind::Comment;
    };
    bool reviewing = false;
    bool basketOpen = true;   // feedback basket panel shown (toggle in diff header)
    bool showApproved = false;
    bool showResolved = false;
    std::vector<Comment> comments;
    int editingComment = -1;
    std::string editingCommentText;
    ReviewCommentKind editingCommentKind = ReviewCommentKind::Comment;
    std::map<std::string, Comment> drafts;
    std::set<std::string> approvedHunks;
    std::map<std::string, std::string> reviewedFiles;
    std::map<std::string, ReviewDecision> verdicts;
    CommitReviewQueue queue;
    std::set<std::string> foldedHunks;
    std::set<std::string> foldedFiles;
    // Inline compose state: the hunk currently being commented on + its buffer.
    std::string composingKey;    // hunk key being commented, empty if none
    std::string composingText;   // in-progress comment text
    std::string composingFile;   // file the comment targets
    std::string composingScope;  // "wt" or a commit SHA
    int composingLine = 0;       // line the comment targets
    int composingEndLine = 0;
    bool composingOldSide = false;
    std::string composingRevision;
    std::string composingCodeContext;
    ReviewCommentKind composingKind = ReviewCommentKind::Comment;
    // Keyboard chunk cursor (vim-style j/k/n nav; a approve, c comment).
    int cursor = 0;              // index of the highlighted visible hunk
    bool cursorMoved = false;
    int hunkCount = 0;           // visible hunks last frame (for clamping)
    bool cursorApprove = false;  // request: approve the cursor hunk
    bool cursorComment = false;  // request: comment on the cursor hunk
    // Set by any durable-state mutation; drained by MainContentSystem which
    // persists the review to disk (see review_store). Not serialized.
    bool dirty = false;
    std::chrono::steady_clock::time_point nextSaveAttempt{};
    std::string storageScope;
    std::string storageRepoPath;
    // "New since you last looked": diff signature of each file when last viewed.
    std::map<std::string, std::string> seenSig;
    // Baseline snapshot for "new since you last looked" (Phase 6).
    std::string baselineHead;     // HEAD sha captured on Embark
    std::string baselineDiffSig;  // signature of the working diff on Embark
    std::string baselineSnapshot;
    async_work::Task<git::GitResult> snapshotFuture;
    bool snapshotCapturing = false;
    int snapshotContext = 3;
    bool snapshotIgnoreWhitespace = false;
    bool sinceReviewOpen = false;
    std::vector<FileDiff> sinceReviewDiff;
    std::string snapshotError;

    static std::string hunk_key(const std::string& filePath,
                                const DiffHunk& hunk) {
        return filePath + "\n" + hunk_signature(hunk);
    }
};

inline void reset_review(ReviewComponent& review) {
    review.reviewing = false;
    review.basketOpen = true;
    review.showApproved = false;
    review.showResolved = false;
    review.comments.clear();
    review.editingComment = -1;
    review.editingCommentText.clear();
    review.editingCommentKind = ReviewCommentKind::Comment;
    review.drafts.clear();
    review.dirty = false;
    review.nextSaveAttempt = {};
    review.storageScope.clear();
    review.storageRepoPath.clear();
    review.approvedHunks.clear();
    review.reviewedFiles.clear();
    review.verdicts.clear();
    review.queue = {};
    review.foldedHunks.clear();
    review.foldedFiles.clear();
    review.composingKey.clear();
    review.composingText.clear();
    review.composingFile.clear();
    review.composingScope.clear();
    review.composingLine = 0;
    review.composingEndLine = 0;
    review.composingOldSide = false;
    review.composingRevision.clear();
    review.composingCodeContext.clear();
    review.composingKind = ReviewCommentKind::Comment;
    review.cursor = 0;
    review.cursorMoved = false;
    review.hunkCount = 0;
    review.cursorApprove = false;
    review.cursorComment = false;
    review.seenSig.clear();
    review.baselineHead.clear();
    review.baselineDiffSig.clear();
    review.baselineSnapshot.clear();
    review.snapshotFuture = {};
    review.snapshotCapturing = false;
    review.sinceReviewOpen = false;
    review.sinceReviewDiff.clear();
    review.snapshotError.clear();
}

inline std::string review_scope(const RepoComponent& repo) {
    return (repo.isDetachedHead ? "detached" : "branch:" + repo.currentBranch) +
        "\nrevision:" + (repo.headCommitHash.empty() ? "unborn" : repo.headCommitHash);
}

inline std::string selected_review_storage_scope(const RepoComponent& repo, const ReviewComponent& review) {
    if (!repo.reviewQueueScope.empty()) return repo.reviewQueueScope;
    if (!repo.comparisonScope().empty()) return repo.comparisonScope();
    if (repo.comparisonEditorOpen) {
        if (review.storageRepoPath == repo.repoPath && !review.storageScope.empty()) return review.storageScope;
    }
    return review_scope(repo);
}

inline std::string diff_signature(const FileDiff& f) {
    std::string s = std::to_string(f.additions) + "," +
                    std::to_string(f.deletions) + "," +
                    std::to_string(f.hunks.size()) + ":" + f.oldMode + ":" + f.newMode + ":" + f.oldPath +
                    ":" + f.oldObject + ":" + f.newObject;
    for (const auto& h : f.hunks) s += "|" + hunk_signature(h);
    return s;
}

inline bool file_reviewed(const ReviewComponent& review, const std::string& scope, const FileDiff& file) {
    auto record = review.reviewedFiles.find(scope + "\n" + file.filePath);
    if ((file.oldMode != file.newMode || !file.oldPath.empty()) &&
        (record == review.reviewedFiles.end() || record->second != diff_signature(file))) return false;
    if (!file.hunks.empty())
        return std::all_of(file.hunks.begin(), file.hunks.end(), [&](const auto& hunk) {
            return review.approvedHunks.contains(scope + "\n" + ReviewComponent::hunk_key(file.filePath, hunk));
        });
    return record != review.reviewedFiles.end() && record->second == diff_signature(file);
}

inline std::vector<size_t> visible_review_file_indices(const std::vector<FileDiff>& files,
    const review_files::Filter& filter, const ReviewComponent* review, const std::string& scope);

inline std::optional<size_t> next_unreviewed_file(const ReviewComponent& review, const std::string& scope,
        const std::vector<FileDiff>& files, const review_files::Filter& filter, const std::string& current) {
    auto indices = visible_review_file_indices(files, filter, &review, scope);
    if (indices.empty()) return std::nullopt;
    auto found = std::find_if(indices.begin(), indices.end(), [&](size_t i) { return files[i].filePath == current; });
    size_t start = found == indices.end() ? indices.size() - 1 : static_cast<size_t>(found - indices.begin());
    for (size_t offset = 1; offset <= indices.size(); ++offset) {
        size_t i = indices[(start + offset) % indices.size()];
        if (!file_reviewed(review, scope, files[i])) return i;
    }
    return std::nullopt;
}

inline std::string comment_location(const ReviewComponent::Comment& comment) {
    auto out = comment.file + ":" + std::to_string(comment.line);
    if (comment.endLine > comment.line) out += "-" + std::to_string(comment.endLine);
    if (comment.oldSide) out += " (old)";
    return out;
}

inline bool save_comment_edit(ReviewComponent& review) {
    if (review.editingComment < 0 || static_cast<size_t>(review.editingComment) >= review.comments.size() ||
        review.editingCommentText.empty()) return false;
    review.comments[review.editingComment].text = review.editingCommentText;
    review.comments[review.editingComment].kind = review.editingCommentKind;
    review.editingComment = -1;
    review.editingCommentText.clear();
    review.dirty = true;
    return true;
}

inline void erase_comment(ReviewComponent& review, size_t index) {
    const auto removed = review.comments.at(index);
    review.comments.erase(review.comments.begin() + static_cast<std::ptrdiff_t>(index));
    if (review.editingComment == static_cast<int>(index)) { review.editingComment = -1; review.editingCommentText.clear(); }
    else if (review.editingComment > static_cast<int>(index)) --review.editingComment;
    if (std::none_of(review.comments.begin(), review.comments.end(), [&](const auto& comment) {
            return comment.scope == removed.scope && comment.file == removed.file;
        })) {
        auto prefix = removed.scope + "\n" + removed.file + "\n";
        std::erase_if(review.foldedHunks, [&](const auto& key) { return key.starts_with(prefix); });
    }
    review.dirty = true;
}

inline size_t unresolved_comment_count(const ReviewComponent& review) {
    return static_cast<size_t>(std::count_if(review.comments.begin(), review.comments.end(),
        [](const auto& comment) { return !comment.resolved; }));
}

inline size_t unresolved_file_count(const ReviewComponent& review, const std::string& scope,
        const std::string& path, const std::string& oldPath = "") {
    return static_cast<size_t>(std::count_if(review.comments.begin(), review.comments.end(), [&](const auto& comment) {
        return !comment.resolved && comment.scope == scope &&
            (comment.file == path || (!oldPath.empty() && comment.oldSide && comment.file == oldPath));
    }));
}

inline bool review_file_visible(const FileDiff& file, const review_files::Filter& filter,
        const ReviewComponent* review, const std::string& scope) {
    return file.isFullContent || (review_files::matches(filter, file.filePath, file_change(file)) &&
        (!filter.onlyUnresolved || (review && unresolved_file_count(*review, scope, file.filePath, file.oldPath) > 0)));
}

inline std::vector<size_t> visible_review_file_indices(const std::vector<FileDiff>& files,
        const review_files::Filter& filter, const ReviewComponent* review, const std::string& scope) {
    auto indices = visible_file_indices(files, filter);
    std::erase_if(indices, [&](size_t i) { return !review_file_visible(files[i], filter, review, scope); });
    return indices;
}

inline std::string unresolved_file_badge(const ReviewComponent& review, const std::string& scope,
        const std::string& path, const std::string& oldPath = "") {
    auto count = unresolved_file_count(review, scope, path, oldPath);
    return count ? " · " + std::to_string(count) + " unresolved" : "";
}

struct ReviewProgress {
    size_t reviewed = 0;
    size_t total = 0;
    size_t unresolved = 0;
    bool can_approve() const { return reviewed == total && unresolved == 0; }
};

inline ReviewProgress review_progress(const ReviewComponent& review, const std::string& scope,
        const std::vector<FileDiff>& files) {
    ReviewProgress progress;
    progress.total = files.size();
    progress.reviewed = static_cast<size_t>(std::count_if(files.begin(), files.end(),
        [&](const auto& file) { return file_reviewed(review, scope, file); }));
    progress.unresolved = static_cast<size_t>(std::count_if(review.comments.begin(), review.comments.end(),
        [&](const auto& comment) { return comment.scope == scope && !comment.resolved; }));
    return progress;
}

inline ReviewProgress review_progress(const ReviewComponent& review, const std::string& scope,
        const std::vector<reading::FileSummary>& files) {
    ReviewProgress progress;
    progress.total = files.size();
    for (const auto& file : files) {
        auto record = review.reviewedFiles.find(scope + "\n" + file.path);
        bool recorded = record != review.reviewedFiles.end() && record->second == file.signature;
        if (file.requiresFileRecord && !recorded) continue;
        bool reviewed = file.hunkKeys.empty() ? recorded : std::all_of(file.hunkKeys.begin(), file.hunkKeys.end(),
            [&](const auto& key) { return review.approvedHunks.contains(scope + "\n" + key); });
        if (reviewed) ++progress.reviewed;
    }
    progress.unresolved = static_cast<size_t>(std::count_if(review.comments.begin(), review.comments.end(),
        [&](const auto& comment) { return comment.scope == scope && !comment.resolved; }));
    return progress;
}

inline std::string review_target_signature(const std::vector<FileDiff>& files) {
    std::string signature;
    for (size_t i : visible_file_indices(files, {}))
        signature += std::to_string(files[i].filePath.size()) + ":" + files[i].filePath + "\n" + diff_signature(files[i]) + "\n";
    return signature;
}

inline ReviewVerdict current_review_verdict(const ReviewComponent& review, const std::string& scope,
        const std::vector<FileDiff>& files) {
    auto decision = review.verdicts.find(scope);
    if (decision == review.verdicts.end() || decision->second.signature != review_target_signature(files)) return ReviewVerdict::InProgress;
    if (decision->second.verdict == ReviewVerdict::Approved && !review_progress(review, scope, files).can_approve()) return ReviewVerdict::InProgress;
    return decision->second.verdict;
}

inline bool review_queue_completion_is_stale(const ReviewComponent& review, const RepoComponent& repo,
        const CommitDetailCache& cache) {
    if (!review.queue.completed.contains(repo.selectedCommitHash()) || !selected_commit_parent(repo).empty()) return false;
    if (cache.cachedCommitHash != repo.selectedCommitHash() || !cache.cachedParentHash.empty()) return false;
    if (cache.patchFuture.valid() || cache.infoFuture.valid() || !cache.commitDetailError.empty()) return false;
    return current_review_verdict(review, repo.selectedCommitHash(), cache.commitDetailDiff) == ReviewVerdict::InProgress;
}

inline ReviewComponent::Comment pending_comment(const ReviewComponent& review) {
    return {review.composingScope, review.composingFile, review.composingLine,
        review.composingText, review.composingEndLine, review.composingOldSide, false,
        review.composingRevision, review.composingCodeContext, review.composingKind};
}

inline ReviewComponent::Comment comment_with_context(ReviewComponent::Comment comment,
    const DiffHunk& hunk, const std::string& head) {
    if (comment.scope == "wt") comment.revision = std::string(comment.oldSide ? "Index" : "Working tree") +
        " at HEAD " + (head.empty() ? "unborn" : head);
    else {
        const auto target = diff_target(comment.scope);
        comment.revision = comment.oldSide ? target.before : target.after;
        if (comment.oldSide && target.kind == DiffTarget::Kind::Commit) comment.revision += " (parent)";
    }
    comment.revision += "; hunk " + hunk_signature(hunk);
    int oldLine = hunk.oldStart, newLine = hunk.newStart;
    for (const auto& line : hunk.lines) {
        char sign = line.empty() ? ' ' : line.front();
        int number = comment.oldSide ? oldLine : newLine;
        bool onSide = comment.oldSide ? sign != '+' : sign != '-';
        if (onSide && number >= comment.line - 3 && number <= std::max(comment.line, comment.endLine) + 3) {
            if (comment.codeContext.size() + line.size() > 16384) {
                comment.codeContext += "[excerpt truncated]\n";
                break;
            }
            comment.codeContext += std::to_string(number) + ": " + (line.empty() ? "" : line.substr(1)) + "\n";
        }
        if (sign != '+') ++oldLine;
        if (sign != '-') ++newLine;
    }
    return comment;
}

inline void begin_comment(ReviewComponent& review, const std::string& key,
                           ReviewComponent::Comment location) {
    if (!review.composingKey.empty()) {
        if (review.composingText.empty()) review.drafts.erase(review.composingKey);
        else review.drafts[review.composingKey] = pending_comment(review);
    }
    if (auto draft = review.drafts.find(key); draft != review.drafts.end()) location = draft->second;
    review.composingKey = key;
    review.composingScope = location.scope;
    review.composingFile = location.file;
    review.composingLine = location.line;
    review.composingEndLine = location.endLine;
    review.composingOldSide = location.oldSide;
    review.composingText = std::move(location.text);
    review.composingRevision = std::move(location.revision);
    review.composingCodeContext = std::move(location.codeContext);
    review.composingKind = location.kind;
    review.dirty = true;
}

// Commit the in-progress comment into the basket and auto-fold its hunk.
inline void commit_pending_comment(ReviewComponent& r) {
    if (r.composingKey.empty()) return;
    if (!r.composingText.empty()) {
        r.comments.push_back(pending_comment(r));
        r.foldedHunks.insert(r.composingKey);
        r.dirty = true;
    }
    r.drafts.erase(r.composingKey);
    r.dirty = true;
    r.composingKey.clear();
    r.composingText.clear();
    r.composingFile.clear();
    r.composingScope.clear();
    r.composingLine = 0;
    r.composingEndLine = 0;
    r.composingOldSide = false;
    r.composingRevision.clear();
    r.composingCodeContext.clear();
}

inline std::string build_review_markdown(const ReviewComponent& review,
                                         const std::string& branch) {
    if (unresolved_comment_count(review) == 0)
        return "";
    std::string out = "## Review of " + branch + "\n";
    // working-tree comments first, then per-commit groups (stable order).
    std::vector<std::string> scopes;
    for (const auto& c : review.comments)
        if (!c.resolved && std::find(scopes.begin(), scopes.end(), c.scope) == scopes.end())
            scopes.push_back(c.scope);
    for (const auto& scope : scopes) {
        out += "\n### " + diff_target_label(scope) + "\n";
        for (const auto& c : review.comments)
            if (c.scope == scope && !c.resolved) {
                out += "\n#### " + comment_location(c) + "\n\n" + c.text + "\n\n";
                out += "Type: " + review_comment_kind_label(c.kind) + "\n";
                out += "Revision: " + (c.revision.empty() ? "not captured for this older comment" : c.revision) + "\n";
                if (c.codeContext.empty()) out += "Code context unavailable for this comment.\n";
                else {
                    size_t longest = 0, run = 0;
                    for (char ch : c.codeContext) {
                        run = ch == '`' ? run + 1 : 0;
                        longest = std::max(longest, run);
                    }
                    std::string fence(std::max(size_t{3}, longest + 1), '`');
                    out += "\nSaved code excerpt (" + std::string(c.oldSide ? "old" : "new") + " side):\n" +
                        fence + "text\n" + c.codeContext + fence + "\n";
                }
            }
    }
    out += "\nApply working-tree feedback to uncommitted changes. Apply commit feedback "
           "as fixups to the named commits. Verify saved excerpts against current code before editing.\n";
    return out;
}

struct BranchDialogState : public afterhours::BaseComponent {
    bool showNewBranchDialog = false;
    std::string newBranchName;
    bool showDeleteBranchDialog = false;
    std::string deleteBranchName;
    bool showForceDeleteDialog = false;
};

struct DiffMatch {
    std::string file;
    int line = 0;
    char sign = ' ';
    size_t column = 0;
};

inline std::vector<DiffMatch> find_diff_matches(const std::vector<FileDiff>& diffs,
                                              const std::string& query) {
    std::vector<DiffMatch> matches;
    if (query.empty()) return matches;
    for (const auto& file : diffs) {
        for (const auto& hunk : file.hunks) {
            int oldLine = hunk.oldStart, newLine = hunk.newStart;
            for (const auto& line : hunk.lines) {
                char sign = line.empty() ? ' ' : line.front();
                int number = sign == '-' ? oldLine : newLine;
                for (size_t at = line.find(query, 1); at != std::string::npos;
                     at = line.find(query, at + query.size()))
                    matches.push_back({file.filePath, number, sign, at - 1});
                if (sign != '+') ++oldLine;
                if (sign != '-') ++newLine;
            }
        }
    }
    return matches;
}

struct LayoutComponent : public afterhours::BaseComponent {
    bool filePickerOpen = false;
    bool filePickerFocus = false;
    std::string filePickerQuery;
    std::string filePickerCacheKey;
    std::vector<std::string> filePickerResults;
    int filePickerIndex = 0;
    bool diffFindOpen = false;
    bool visibleWhitespace = false;
    bool diffFindFocus = false;
    std::string diffFindQuery;
    int diffFindIndex = 0;
    int diffFindNavigate = 0;
    static constexpr float kDefaultSidebarWidth = 280.0f;
    static constexpr float kCommitSplitterHeight = 16.f;
    float sidebarWidth = kDefaultSidebarWidth;
    float sidebarMinWidth = 200.0f;
    float commitLogRatio = 0.4f;

    enum class SidebarMode { Changes, Refs };
    SidebarMode sidebarMode = SidebarMode::Changes;
    enum class SidebarNavigation { Review, Files };
    SidebarNavigation sidebarNavigation = SidebarNavigation::Review;
    // Review tabs within the Changes view (mock: To review / Approved / Untracked).
    enum class ReviewTab { ToReview, Staged, Untracked };
    ReviewTab reviewTab = ReviewTab::ToReview;

    enum class FileViewMode { Flat, Tree, All };
    FileViewMode fileViewMode = FileViewMode::Flat;
    std::map<std::string, std::set<std::string>> collapsedDirectories;

    enum class DiffViewMode { Inline, SideBySide };
    DiffViewMode diffViewMode = DiffViewMode::Inline;
    bool commitMetadataExpanded = false;
    bool diffOptionsOpen = false;
    bool shortcutsOpen = false;

    bool sidebarVisible = true;
    bool commandLogVisible = false;
    // Shelf: when collapsed, the diff pane is hidden and the sidebar fills the
    // window (Bear-like). Derived each frame from whether anything is selected;
    // set by LayoutUpdateSystem and read by the sidebar/main-content renderers.
    bool shelfCollapsed = false;
    // Starts true because the window itself opens at the shelf width. If the
    // first frame finds something selected (a restored review, say), the
    // true->false transition expands it, same as any later selection.
    bool lastShelfCollapsed = true;   // for detecting collapse/expand transitions
    // The window opens at the default sidebar width, but the saved one is only
    // known after settings load, so the first frame corrects the difference.
    bool didInitialWidthSync = false;
    float reviewPanelWidth = 920.f;

    float commandLogHeight = 200.0f;

    struct Rect { float x=0, y=0, width=0, height=0; };
    Rect tabStrip{};
    Rect menuBar{};
    Rect toolbar{};
    Rect sidebar{};
    Rect sidebarFiles{};
    Rect sidebarLog{};
    Rect mainContent{};
    Rect contentTabs{};
    Rect feedback{};
    Rect commandLog{};
    Rect statusBar{};
};

struct CommitEditorComponent : public afterhours::BaseComponent {
    std::string subject;
    std::string body;
    bool isVisible = false;
    bool isAmend = false;

    enum class UnstagedPolicy { Ask, StageAll, CommitStagedOnly };
    UnstagedPolicy unstagedPolicy = UnstagedPolicy::Ask;

    // Commit workflow state (T030)
    bool commitRequested = false;      // Set true to initiate commit flow
    bool showUnstagedDialog = false;   // Controls modal visibility
    bool rememberChoice = false;       // "Remember this choice" checkbox state
};

struct MenuComponent : public afterhours::BaseComponent {
    int activeMenuIndex = -1;

    enum class PendingDialog { None, OpenRepo };
    PendingDialog pendingDialog = PendingDialog::None;

    struct Notice {
        enum class Kind { Info, Success, Error };
        std::string message;
        Kind kind = Kind::Info;
    };
    std::vector<Notice> pendingToasts;
};

struct CommandLogComponent : public afterhours::BaseComponent {
    struct Entry {
        std::string command;
        std::string output;
        std::string error;
        bool success = false;
        double timestamp = 0.0;
    };
    std::vector<Entry> entries;
};

// ---- Tab Components ----

struct ActiveTab : public afterhours::BaseComponent {};

struct Tab : public afterhours::BaseComponent {
    std::string label = "Untitled";

    LayoutComponent::SidebarMode sidebarMode = LayoutComponent::SidebarMode::Changes;
    LayoutComponent::FileViewMode fileViewMode = LayoutComponent::FileViewMode::Flat;
    LayoutComponent::DiffViewMode diffViewMode = LayoutComponent::DiffViewMode::Inline;
    bool sidebarVisible = true;
};

struct TabStripComponent : public afterhours::BaseComponent {
    std::vector<afterhours::EntityID> tabOrder;
};

// Tracks in-flight network git operations (push/pull/fetch) so the UI
// thread is never blocked.  Fire-and-forget: the polling system handles
// completion, toast notifications, and refresh triggers.
struct PendingNetworkOp {
    std::string label;
    async_work::Task<git::GitResult> future;
    afterhours::EntityID tabId{0};
};

struct NetworkOpsComponent : public afterhours::BaseComponent {
    std::vector<PendingNetworkOp> pending;
};

} // namespace ecs

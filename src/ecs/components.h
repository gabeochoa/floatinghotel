#pragma once

#include <algorithm>
#include <cstdint>
#include <future>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../../vendor/afterhours/src/core/entity_helper.h"
#include "../git/git_runner.h"
#include "../git/history_query.h"

namespace ecs {

struct SearchMatch {
    std::string file;
    int line = 0;
    std::string text;
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
};

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

struct RepoComponent : public afterhours::BaseComponent {
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

    std::string selectedFilePath;
    std::string selectedCommitHash;
    std::vector<FileDiff> currentDiff;
    std::vector<FileDiff> stagedDiff;
    bool selectedFileStaged = false;

    std::string cachedFilePath;

    bool refreshRequested = false;
    bool ignoreWhitespace = false;
    int diffContext = 3;
    std::string fullFilePath;
    std::string fullFileRevision;
    std::string fullFileCacheKey;
    std::vector<FileDiff> fullFileDiff;
    std::string fullFileError;
    bool isRefreshing = false;
    bool hasLoadedOnce = false;
    unsigned repoVersion = 0;
    unsigned dataGeneration = 0;
    std::vector<std::string> allFilePaths;
    std::string filesError;
    bool repoSearchOpen = false;
    bool repoSearchFocus = false;
    std::string repoSearchQuery;
    std::string repoSearchPath;
    std::string repoSearchError;
    std::shared_future<git::GitResult> repoSearchFuture;
    std::vector<SearchMatch> repoSearchResults;
    int fullFileTargetLine = 0;
    int fullFileNavigateFrames = 0;
    bool fileHistoryOpen = false;
    std::string fileHistoryPath;
    std::string fileHistoryRevision;
    std::string fileHistoryError;
    int fileHistoryLimit = 200;
    std::shared_future<git::GitResult> fileHistoryFuture;
    std::vector<CommitEntry> fileHistoryEntries;
    std::shared_future<git::GitResult> blameFuture;
    BlameLine blameLine;
    std::string blameError;
    bool blameOpen = false;
    bool commitSearchOpen = false;
    git::HistoryQuery commitSearchQuery;
    int commitSearchLimit = 200;
    std::shared_future<git::GitResult> commitSearchFuture;
    std::vector<CommitEntry> commitSearchEntries;
    std::string commitSearchError;
    bool comparisonOpen = false;
    std::string comparisonBase;
    std::string comparisonTarget;
    bool comparisonMergeBase = false;
    std::shared_future<git::RevisionComparison> comparisonFuture;
    std::vector<FileDiff> comparisonDiff;
    std::string comparisonScope;
    std::string comparisonError;
    int comparisonContext = 3;
    bool comparisonIgnoreWhitespace = false;
    std::string diffTargetFile;
    int diffTargetFrames = 0;
};

struct CommitDetailCache : public afterhours::BaseComponent {
    std::string cachedCommitHash;
    std::vector<FileDiff> commitDetailDiff;
    std::string commitDetailBody;
    std::string commitDetailAuthorEmail;
    std::string commitDetailParents;
    std::string commitDetailError;
};

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
    };
    bool reviewing = false;
    bool basketOpen = true;   // feedback basket panel shown (toggle in diff header)
    bool showApproved = false;
    bool showResolved = false;
    std::vector<Comment> comments;
    int editingComment = -1;
    std::string editingCommentText;
    std::set<std::string> approvedHunks;
    std::set<std::string> foldedHunks;
    // Inline compose state: the hunk currently being commented on + its buffer.
    std::string composingKey;    // hunk key being commented, empty if none
    std::string composingText;   // in-progress comment text
    std::string composingFile;   // file the comment targets
    std::string composingScope;  // "wt" or a commit SHA
    int composingLine = 0;       // line the comment targets
    int composingEndLine = 0;
    bool composingOldSide = false;
    // Keyboard chunk cursor (vim-style j/k/n nav; a approve, c comment).
    int cursor = 0;              // index of the highlighted visible hunk
    bool cursorMoved = false;
    int hunkCount = 0;           // visible hunks last frame (for clamping)
    bool cursorApprove = false;  // request: approve the cursor hunk
    bool cursorComment = false;  // request: comment on the cursor hunk
    // Set by any durable-state mutation; drained by MainContentSystem which
    // persists the review to disk (see review_store). Not serialized.
    bool dirty = false;
    // "New since you last looked": diff signature of each file when last viewed.
    std::map<std::string, std::string> seenSig;
    // Baseline snapshot for "new since you last looked" (Phase 6).
    std::string baselineHead;     // HEAD sha captured on Embark
    std::string baselineDiffSig;  // signature of the working diff on Embark

    static std::string hunk_key(const std::string& filePath,
                                const DiffHunk& hunk) {
        return filePath + "\n" + hunk_signature(hunk);
    }
};

inline std::string diff_signature(const FileDiff& f) {
    std::string s = std::to_string(f.additions) + "," +
                    std::to_string(f.deletions) + "," +
                    std::to_string(f.hunks.size());
    for (const auto& h : f.hunks) s += "|" + hunk_signature(h);
    return s;
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
    review.editingComment = -1;
    review.editingCommentText.clear();
    review.dirty = true;
    return true;
}

inline size_t unresolved_comment_count(const ReviewComponent& review) {
    return static_cast<size_t>(std::count_if(review.comments.begin(), review.comments.end(),
        [](const auto& comment) { return !comment.resolved; }));
}

// Commit the in-progress comment into the basket and auto-fold its hunk.
inline void commit_pending_comment(ReviewComponent& r) {
    if (r.composingKey.empty()) return;
    if (!r.composingText.empty()) {
        r.comments.push_back({r.composingScope, r.composingFile,
                              r.composingLine, r.composingText,
                              r.composingEndLine, r.composingOldSide});
        r.foldedHunks.insert(r.composingKey);
        r.dirty = true;
    }
    r.composingKey.clear();
    r.composingText.clear();
    r.composingFile.clear();
    r.composingScope.clear();
    r.composingLine = 0;
    r.composingEndLine = 0;
    r.composingOldSide = false;
}

// Build the batch-review markdown written to /tmp/floatinghotel-review.md and
// copied to the clipboard. Groups comments by scope (working tree vs commit SHA)
// so the agent knows exactly which diff each comment targets.
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
        out += (scope == "wt") ? "\n### working tree (uncommitted)\n"
                               : "\n### commit " + scope + "\n";
        for (const auto& c : review.comments)
            if (c.scope == scope && !c.resolved)
                out += "- " + comment_location(c) + " \xe2\x80\x94 " +
                       c.text + "\n";
    }
    out += "\n(agent: apply each as a fixup to the named commit, not on top of "
           "the stack)\n";
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
    static constexpr float kDefaultSidebarWidth = 340.0f;
    float sidebarWidth = kDefaultSidebarWidth;
    float sidebarMinWidth = 200.0f;
    float commitLogRatio = 0.4f;

    enum class SidebarMode { Changes, Refs };
    SidebarMode sidebarMode = SidebarMode::Changes;
    // Review tabs within the Changes view (mock: To review / Approved / Untracked).
    enum class ReviewTab { ToReview, Staged, Untracked };
    ReviewTab reviewTab = ReviewTab::ToReview;

    enum class FileViewMode { Flat, Tree, All };
    FileViewMode fileViewMode = FileViewMode::Flat;
    std::map<std::string, std::set<std::string>> collapsedDirectories;

    enum class DiffViewMode { Inline, SideBySide };
    DiffViewMode diffViewMode = DiffViewMode::Inline;

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
    int expandedWidth = 0;            // window width to restore when expanding
    // Smooth tray animation: window width is tweened frame-by-frame (each step an
    // instant resize) and the whole UI is laid out at the animated width, so the
    // window + content move together (like the HTML mock's CSS width transition).
    bool animating = false;
    float animFrom = 0.f;
    float animTarget = 0.f;
    float animT = 1.f;

    float commandLogHeight = 200.0f;

    struct Rect { float x=0, y=0, width=0, height=0; };
    Rect tabStrip{};
    Rect menuBar{};
    Rect toolbar{};
    Rect sidebar{};
    Rect sidebarFiles{};
    Rect sidebarLog{};
    Rect mainContent{};
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

    std::string pendingToast;
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
    std::future<git::GitResult> future;
    afterhours::EntityID tabId{0};
};

struct NetworkOpsComponent : public afterhours::BaseComponent {
    std::vector<PendingNetworkOp> pending;
};

} // namespace ecs

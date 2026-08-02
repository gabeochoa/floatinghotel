#pragma once

#include <algorithm>
#include <future>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/base_component.h"
#include "../../vendor/afterhours/src/core/entity_helper.h"

namespace git { struct GitResult; }

namespace ecs {

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
    bool isSubmodule = false;  // gitlink change (index mode 160000)
    std::vector<DiffHunk> hunks;
};

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

    // Branch data (T031)
    std::vector<BranchInfo> branches;

    std::string selectedFilePath;
    std::string selectedCommitHash;
    std::vector<FileDiff> currentDiff;

    std::string cachedFilePath;

    bool refreshRequested = false;
    bool isRefreshing = false;
    bool hasLoadedOnce = false;
    unsigned repoVersion = 0;
};

struct CommitDetailCache : public afterhours::BaseComponent {
    std::string cachedCommitHash;
    std::vector<FileDiff> commitDetailDiff;
    std::string commitDetailBody;
    std::string commitDetailAuthorEmail;
    std::string commitDetailParents;
};

// Per-tab "Ballroom" review state (see docs/mocks/ballroom.html).
// Approve/fold sets are keyed by content (filePath + "\n" + hunk.header) so they
// survive the diff being rebuilt on every git refresh (hunks have no stable id).
struct ReviewComponent : public afterhours::BaseComponent {
    struct Comment {
        std::string scope;  // "wt" for working tree, or a commit SHA
        std::string file;
        int line = 0;
        std::string text;
    };
    bool reviewing = false;
    bool basketOpen = true;   // feedback basket panel shown (toggle in diff header)
    std::vector<Comment> comments;
    std::set<std::string> approvedHunks;
    std::set<std::string> foldedHunks;
    // Inline compose state: the hunk currently being commented on + its buffer.
    std::string composingKey;    // hunk key being commented, empty if none
    std::string composingText;   // in-progress comment text
    std::string composingFile;   // file the comment targets
    std::string composingScope;  // "wt" or a commit SHA
    int composingLine = 0;       // line the comment targets
    // Keyboard chunk cursor (vim-style j/k/n nav; a approve, c comment).
    int cursor = 0;              // index of the highlighted visible hunk
    int hunkCount = 0;           // visible hunks last frame (for clamping)
    bool cursorApprove = false;  // request: approve the cursor hunk
    bool cursorComment = false;  // request: comment on the cursor hunk
    // "New since you last looked": diff signature of each file when last viewed.
    std::map<std::string, std::string> seenSig;
    // Baseline snapshot for "new since you last looked" (Phase 6).
    std::string baselineHead;     // HEAD sha captured on Embark
    std::string baselineDiffSig;  // signature of the working diff on Embark

    static std::string hunk_key(const std::string& filePath,
                                const std::string& header) {
        return filePath + "\n" + header;
    }
};

// A coarse signature of a file's diff — changes when the agent reworks it.
inline std::string diff_signature(const FileDiff& f) {
    std::string s = std::to_string(f.additions) + "," +
                    std::to_string(f.deletions) + "," +
                    std::to_string(f.hunks.size());
    for (const auto& h : f.hunks) s += "|" + h.header;
    return s;
}

// Commit the in-progress comment into the basket and auto-fold its hunk.
inline void commit_pending_comment(ReviewComponent& r) {
    if (r.composingKey.empty()) return;
    if (!r.composingText.empty()) {
        r.comments.push_back({r.composingScope, r.composingFile,
                              r.composingLine, r.composingText});
        r.foldedHunks.insert(r.composingKey);
    }
    r.composingKey.clear();
    r.composingText.clear();
    r.composingFile.clear();
    r.composingScope.clear();
    r.composingLine = 0;
}

// Build the batch-review markdown written to /tmp/floatinghotel-review.md and
// copied to the clipboard. Groups comments by scope (working tree vs commit SHA)
// so the agent knows exactly which diff each comment targets.
inline std::string build_review_markdown(const ReviewComponent& review,
                                         const std::string& branch) {
    if (review.comments.empty())
        return "";
    std::string out = "## Review of " + branch + "\n";
    // working-tree comments first, then per-commit groups (stable order).
    std::vector<std::string> scopes;
    for (const auto& c : review.comments)
        if (std::find(scopes.begin(), scopes.end(), c.scope) == scopes.end())
            scopes.push_back(c.scope);
    for (const auto& scope : scopes) {
        out += (scope == "wt") ? "\n### working tree (uncommitted)\n"
                               : "\n### commit " + scope + "\n";
        for (const auto& c : review.comments)
            if (c.scope == scope)
                out += "- " + c.file + ":" + std::to_string(c.line) + " \xe2\x80\x94 " +
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

struct LayoutComponent : public afterhours::BaseComponent {
    float sidebarWidth = 340.0f;
    float sidebarMinWidth = 200.0f;
    float commitLogRatio = 0.4f;

    enum class SidebarMode { Changes, Refs };
    SidebarMode sidebarMode = SidebarMode::Changes;
    // Review tabs within the Changes view (mock: To review / Approved / Untracked).
    enum class ReviewTab { ToReview, Approved, Untracked };
    ReviewTab reviewTab = ReviewTab::ToReview;

    enum class FileViewMode { Flat, Tree, All };
    FileViewMode fileViewMode = FileViewMode::Flat;

    enum class DiffViewMode { Inline, SideBySide };
    DiffViewMode diffViewMode = DiffViewMode::Inline;

    bool sidebarVisible = true;
    bool commandLogVisible = false;
    // Shelf: when collapsed, the diff pane is hidden and the sidebar fills the
    // window (Bear-like). Derived each frame from whether anything is selected;
    // set by LayoutUpdateSystem and read by the sidebar/main-content renderers.
    bool shelfCollapsed = false;
    bool lastShelfCollapsed = false;  // for detecting collapse/expand transitions
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

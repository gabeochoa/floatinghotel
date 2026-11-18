#pragma once

#include "../util/reading_load.h"
#include <atomic>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <afterhours/src/core/base_component.h>
#include <afterhours/src/core/entity_helper.h>
#include "../git/git_runner.h"
#include "../git/history_query.h"
#include "../git/history_scope.h"
#include "../git/reading_catalog.h"
#include "../git/push_destination.h"
#include "../git/path_list.h"
#include "../lsp/semantic.h"
#include "../git/source_position.h"
#include "../util/file_query.h"
#include "../util/search_results.h"
#include "../util/codeowners.h"
#include "../util/code_bookmark.h"
#include "../util/hex_view.h"
#include "../util/markdown_preview.h"
#include "../util/focus_target.h"
#include "../util/tree_navigation.h"
#include "../util/diff_revisions.h"
#include "../util/reading_workspace.h"
#include "../util/code_motion.h"
#include "../util/loading_feedback.h"
#include "../util/commit_prefetch.h"
#include "../util/code_lexer.h"
#include "../util/outline.h"
#include "../util/reading_anchor.h"
#include "../util/review_files.h"
#include "../util/review_comment_kind.h"
#include "../util/review_verdict.h"
#include "../util/async_task.h"
#include "../util/async_slot.h"
#include "../util/refresh_scope.h"
#include "../util/history_selection.h"
#include "../util/repo_picker.h"

namespace ecs {

struct BlameLine {
    std::string hash;
    std::string author;
    std::string date;  // YYYY-MM-DD (author date)
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

inline std::uint64_t next_render_identity() {
    static std::atomic<std::uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

struct DiffHunk {
    int oldStart = 0, oldCount = 0;
    int newStart = 0, newCount = 0;
    std::string header;        // The @@ line
    std::vector<std::string> lines; // Lines with +/-/space prefix
    std::set<size_t> noNewline;
    std::set<size_t> movedLines;
    std::vector<code_lexer::State> syntaxBefore, syntaxAfter;
    std::uint64_t renderIdentity = next_render_identity();
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
    bool syntaxResolved = false;
};

struct FilePageCursor {
    uint64_t offset = 0;
    int line = 1;
    bool continuation = false;
    int column = 1;
    code_lexer::State lexical;
    bool operator==(const FilePageCursor&) const = default;
};

struct FilePageRequest {
    enum class Action { Start, Next, Previous, TargetLine };
    Action action = Action::Start;
    FilePageCursor cursor;
    int targetLine = 0;
    std::string sourceIdentity;
    int leadingLines = 0;
    int targetColumn = 1;
};

struct FilePage {
    FilePageCursor begin;
    FilePageCursor next;
    uint64_t totalBytes = 0;
    std::string blob;
    std::string encoding;
    std::string sourceIdentity;

    bool contains(int line, int column, int length = 1) const {
        return line >= begin.line && (line > begin.line || column >= begin.column) &&
            (line < next.line || (line == next.line && static_cast<int64_t>(column) + length <= next.column));
    }
};

struct FullFileContent {
    FileDiff diff;
    std::string raw;
    std::string error;
    FilePage page;
    std::string encodingLabel;
    std::string decodedText;
    std::string resolvedRevision;
    reading_load::Trace trace;
};

struct SourcePageWindow {
    std::vector<FilePage> pages;
    std::string raw;
};

// Full-file paged reading (fsm_todo.md M5): the two concatenated cache-key
// strings built in full_file_view.h (source key at ~:103 and page key at
// ~:127, rebuilt for a resolved revision at ~:185) are one typed PageKey.
// `source` is the file identity (repo/revision/path plus the data
// generation only for working-tree/INDEX reads, exactly as the old source
// string appended it only then); `encoding`, `action`, `cursor` and
// `target` are the remaining old page-key fields. Equality is the load
// identity: any field differing is a different page load.
struct FileSource {
    std::string repo;
    std::string revision;
    std::string path;
    unsigned dataGeneration = 0;
    bool operator==(const FileSource&) const = default;
};

inline FileSource make_file_source(std::string repo, std::string revision,
                                   std::string path, unsigned dataGeneration) {
    const unsigned effective = (revision.empty() || revision == "INDEX") ? dataGeneration : 0;
    return {std::move(repo), std::move(revision), std::move(path), effective};
}

struct PageTarget {
    int line = 0;
    int leadingLines = 0;
    int column = 1;
    bool operator==(const PageTarget&) const = default;
};

struct PageKey {
    FileSource source;
    std::string encoding;
    FilePageRequest::Action action = FilePageRequest::Action::Start;
    FilePageCursor cursor;
    PageTarget target;
    bool operator==(const PageKey&) const = default;
};

inline PageKey make_page_key(FileSource source, std::string encoding,
                             const FilePageRequest& request) {
    return {std::move(source), std::move(encoding), request.action, request.cursor,
            {request.targetLine, request.leadingLines, request.targetColumn}};
}

// Serialised form for UI-only identities (markdown cache, reading-view
// binding, hex preview labelling). Never compared for load identity.
inline std::string page_key_string(const PageKey& key) {
    return key.source.repo + "\n" + key.source.revision + "\n" + key.source.path + ":" +
        std::to_string(key.source.dataGeneration) + "\n" + key.encoding + ":" +
        std::to_string(static_cast<int>(key.action)) + ":" + std::to_string(key.cursor.offset) + ":" +
        std::to_string(key.target.line) + ":" + std::to_string(key.target.leadingLines) + ":" +
        std::to_string(key.target.column);
}

// FileLoad lifecycle: Idle (no page requested) -> Loading (cold page in
// flight, spinner after the 150 ms delay) -> Ready (page landed) or
// Failed (landed read reported an error). A cache peek lands at once and
// its validating read runs behind it: Ready -> Revalidating while that
// read is in flight, back to Ready when it agrees or its replacement
// page lands. A Next/Previous page that extends the held window is
// Extending (old page stays on screen, gated on scroll quiescence in
// the poll) instead of Loading. All phase transitions happen in
// render_full_file (the poll in full_file_view.h); other code only
// requests a page (request_page) or drops the load (invalidate/reset).
enum class FileLoad { Idle, Loading, Revalidating, Extending, Ready, Failed };

inline std::string_view file_load_name(FileLoad phase) {
    switch (phase) {
        case FileLoad::Idle: return "Idle";
        case FileLoad::Loading: return "Loading";
        case FileLoad::Revalidating: return "Revalidating";
        case FileLoad::Extending: return "Extending";
        case FileLoad::Ready: return "Ready";
        case FileLoad::Failed: return "Failed";
    }
    return "Idle";
}

struct FullFileLoad {
    FileLoad phase = FileLoad::Idle;
    std::optional<FileSource> source;
    std::optional<PageKey> key;
    FilePageRequest request;
    async_work::Task<FullFileContent> task;
    async_work::Task<FullFileContent> revalidate;
    reading::RequestStamp stamp;
    loading_feedback::Delay loading;
    std::string error;

    bool busy() const { return phase == FileLoad::Loading || phase == FileLoad::Extending; }
    bool extending() const { return phase == FileLoad::Extending; }
    bool revalidating() const { return phase == FileLoad::Revalidating; }
    bool failed() const { return phase == FileLoad::Failed; }
    bool task_in_flight() const {
        return task.valid() && task.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }
    bool revalidate_in_flight() const {
        return revalidate.valid() && revalidate.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    }
    bool needs_load(const PageKey& desired) const { return !key || *key != desired; }
    // Poll-only: start a cold (Loading) or window-extending (Extending)
    // load for `nextKey`. A peeked page arrives via `nextTask` already
    // completed with `nextRevalidate` validating behind it.
    void begin(PageKey nextKey, reading::RequestStamp nextStamp,
               async_work::Task<FullFileContent> nextTask,
               async_work::Task<FullFileContent> nextRevalidate, bool extend) {
        source = nextKey.source;
        key = std::move(nextKey);
        stamp = std::move(nextStamp);
        task = std::move(nextTask);
        revalidate = std::move(nextRevalidate);
        error.clear();
        loading.restart();
        phase = extend ? FileLoad::Extending : FileLoad::Loading;
    }
    // Poll-only: a landed page. A still-pending revalidate behind a peek
    // keeps the load Revalidating; a landed error is Failed.
    void land() {
        phase = !error.empty() ? FileLoad::Failed
                : revalidate.valid() ? FileLoad::Revalidating : FileLoad::Ready;
    }
    void fail(std::string message) {
        error = std::move(message);
        phase = FileLoad::Failed;
    }
    // Poll-only: the validating read agreed (or was dropped), so a
    // Revalidating load with nothing behind it settles Ready.
    void settle_revalidation() {
        if (phase == FileLoad::Revalidating && !revalidate.valid() && !task.valid())
            phase = FileLoad::Ready;
    }
    // Request a different page from outside the poll: forget the key so
    // the next poll begins the load. Held data, tasks and phase are left
    // for the poll's begin() to replace, exactly as clearing the old
    // cache-key string did.
    void request_page(FilePageRequest next) {
        request = std::move(next);
        key.reset();
    }
    void invalidate_key() { key.reset(); }
    // Drop only the in-flight main task and its key (navigation within
    // the same document): request, held page and revalidate are kept,
    // as clearing the old future+cache-key pair did.
    void drop_task() {
        task = {};
        key.reset();
    }
    // Poll-only: a landed result for a superseded stamp is dropped; the
    // next poll begins a fresh load for the desired key.
    void drop_stale() {
        key.reset();
        phase = FileLoad::Idle;
    }
    // Drop the whole load (document left, reset, hidden tab): back to
    // Idle with no key, task, stamp or error retained.
    void reset() {
        phase = FileLoad::Idle;
        source.reset();
        key.reset();
        request = {};
        task = {};
        revalidate = {};
        stamp = {};
        error.clear();
    }
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
    std::string metadata;
    std::vector<FileDiff> files;
    std::string error;
    std::string resolvedCommit;
    std::string resolvedParent;
    reading_load::Trace trace;
};

// Identity of a commit-detail patch load (M6): the five fields that used
// to be hand-compared as cachedCommitHash/cachedParentHash/cachedRepoPath/
// cachedContext/cachedIgnoreWhitespace are now one struct key on the slot.
struct PatchKey {
    std::string repo;
    std::string commit;
    std::string parent;
    int context = 3;
    bool ignoreWs = false;
    bool operator==(const PatchKey&) const = default;
};

struct UntrackedReviewFiles {
    std::vector<FileDiff> files;
    std::string notice;
};

// Untracked review sub-read of refresh (fsm_todo.md M4): the former
// optional<unsigned> generation-as-dirty-flag, repository string and raw
// future are one named read. `Stale` means the previews in currentDiff do
// not cover the recorded generation/repository (never read, invalidated
// by a landed whole-repo diff or a seed, or the data generation moved);
// `Loading` is the read in flight; `Ready` is a landed read for the
// recorded generation/repository. The refresh system is the only driver:
// it invalidates a stale in-flight read, polls a landed one (appending
// deduped by path, as before), and begins the next read when the repo
// has loaded once and this read is not current for it. Rendering still
// reads currentDiff and `notice`, exactly as before.
enum class UntrackedReviewPhase { Stale, Loading, Ready };

inline std::string_view untracked_review_phase_name(UntrackedReviewPhase phase) {
    switch (phase) {
        case UntrackedReviewPhase::Stale: return "Stale";
        case UntrackedReviewPhase::Loading: return "Loading";
        case UntrackedReviewPhase::Ready: return "Ready";
    }
    return "Stale";
}

struct UntrackedReviewRead {
    UntrackedReviewPhase phase = UntrackedReviewPhase::Stale;
    async_work::Task<UntrackedReviewFiles> task;
    unsigned generation = 0;
    std::string repository;
    std::string notice;

    bool busy() const { return phase == UntrackedReviewPhase::Loading; }
    bool current_for(unsigned dataGeneration, std::string_view repoPath) const {
        return phase == UntrackedReviewPhase::Ready && generation == dataGeneration &&
            repository == repoPath;
    }
    bool needs_read(unsigned dataGeneration, std::string_view repoPath) const {
        return !current_for(dataGeneration, repoPath);
    }
    // An in-flight read asked about an older generation/repository.
    bool stale_in_flight(unsigned dataGeneration, std::string_view repoPath) const {
        return busy() && (generation != dataGeneration || repository != repoPath);
    }
    void begin(unsigned dataGeneration, std::string repoPath,
               async_work::Task<UntrackedReviewFiles> nextTask) {
        generation = dataGeneration;
        repository = std::move(repoPath);
        notice.clear();
        task = std::move(nextTask);
        phase = UntrackedReviewPhase::Loading;
    }
    // No untracked files to read: record the generation as covered.
    void settle(unsigned dataGeneration, std::string repoPath) {
        generation = dataGeneration;
        repository = std::move(repoPath);
        notice.clear();
        task = {};
        phase = UntrackedReviewPhase::Ready;
    }
    // Consume a landed read; the notice rides along on the read itself.
    std::optional<UntrackedReviewFiles> poll() {
        if (!busy() || !task.valid()) return std::nullopt;
        if (task.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return std::nullopt;
        auto value = task.get();
        task = {};
        notice = value.notice;
        phase = UntrackedReviewPhase::Ready;
        return value;
    }
    // Drop back to Stale (landed diff discarded the previews, seed, or a
    // stale in-flight read). The notice is kept until the next begin, as
    // the former flag reset kept it.
    void invalidate() {
        task = {};
        phase = UntrackedReviewPhase::Stale;
    }
};

// Selected-file diff source (fsm_todo.md M3): one file's diff is served
// from the whole-repo diff (`Whole`), streamed ahead of it while it is
// still landing (`SinglePending`), served from that single-file read
// (`Single`), or the single-file read failed (`Failed` — rendered as an
// honest error instead of silently keeping the previous file's diff).
// The ~0u sentinel generation compare is gone: whole-repo readiness is
// the optional generations on RepoComponent below. Only the main-content
// renderer drives transitions; rendering still reads `diff`.
enum class DiffSource { Whole, SinglePending, Single, Failed };

inline std::string_view diff_source_name(DiffSource source) {
    switch (source) {
        case DiffSource::Whole: return "Whole";
        case DiffSource::SinglePending: return "SinglePending";
        case DiffSource::Single: return "Single";
        case DiffSource::Failed: return "Failed";
    }
    return "Whole";
}

struct SelectedDiffRead {
    DiffSource source = DiffSource::Whole;
    std::string key;
    async_work::Task<git::GitResult> task;
    std::optional<FileDiff> diff;
    std::string error;

    bool pending() const { return source == DiffSource::SinglePending; }
    bool failed() const { return source == DiffSource::Failed; }
    bool ready() const {
        return pending() && task.valid() &&
            task.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
    void begin(std::string nextKey, async_work::Task<git::GitResult> nextTask) {
        key = std::move(nextKey);
        task = std::move(nextTask);
        diff.reset();
        error.clear();
        source = DiffSource::SinglePending;
    }
    // Consume the in-flight task; the caller checks the key still matches
    // before landing or failing, a mismatched result is simply dropped.
    git::GitResult take() {
        auto value = task.get();
        task = {};
        return value;
    }
    void land(std::optional<FileDiff> landed) {
        task = {};
        diff = std::move(landed);
        error.clear();
        source = DiffSource::Single;
    }
    void fail(std::string message) {
        task = {};
        diff.reset();
        error = std::move(message);
        source = DiffSource::Failed;
    }
    // The whole-repo diff for the current generation landed and takes
    // over; the streamed single-file read is dropped.
    void use_whole() {
        key.clear();
        task = {};
        diff.reset();
        error.clear();
        source = DiffSource::Whole;
    }
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

struct ReadingLayoutRow {
    int entity;
    std::string path;
    int oldStart = 0, oldEnd = 0, newStart = 0, newEnd = 0;
    int column = 1, endColumn = std::numeric_limits<int>::max();
    bool folded = false;
};

struct ReadingLayout {
    int entity = -1;
    int editorEntity = -1;
    bool revealEditor = false;
    std::string key;
    std::string previousKey;
    reading::DocumentId previousDocument;
    std::optional<reading::RequestStamp> request;
    float width = 0.f;
    float height = 0.f;
    float contentHeight = 0.f;
    float offset = 0.f;
    bool ready = false;
    bool bound = false;
    bool wasRevealing = false;
    bool codeRows = true;
    std::optional<int> projectedLine;
    std::vector<ReadingLayoutRow> rows;
};

// Revision-comparison lifecycle (fsm_todo.md M7): Closed -> Editing
// (the comparison editor form is open) -> Loading (a compare read is in
// flight) -> Loaded (the resolved comparison diff is held) | Failed
// (the error is retained and rendered). The stored phase is the only
// open state: `comparisonOpen()` reads it instead of deriving open-ness
// from the workspace destination plus the editor modal. Transitions are
// `open_comparison_editor`/`begin_comparison_load`/`finish_comparison_load`/
// `fail_comparison`/`close_comparison`, plus `sync_comparison_phase()` —
// the one navigation/editor-dismiss transition, driven from
// `navigation::finish` — mirroring `focus::State::sync`. The form fields
// (comparisonBase/Target/MergeBase) stay data: a Document load ignores
// unsubmitted edits, a SubmittedForm load is keyed by them (see
// `navigation::comparison_request_key`).
// Repository search lifecycle (fsm_todo.md M10): Closed (panel shut) ->
// Debouncing (text changed, 150 ms pause owed) -> Running (grep in
// flight, batches streaming) -> Done (results or no matches) | Failed
// (error retained, distinct from no matches). External is a first-class
// landing for language-server results injected by semantic navigation:
// they are not a text search, so the panel does not re-search over them,
// but editing the text leaves External for Debouncing like any edit.
// All phase writes go through the RepoComponent search transitions;
// rendering reads results/groups/error/source as data.
enum class Search { Closed, Debouncing, Running, Done, Failed, External };

inline std::string_view search_name(Search search) {
    switch (search) {
        case Search::Closed: return "Closed";
        case Search::Debouncing: return "Debouncing";
        case Search::Running: return "Running";
        case Search::Done: return "Done";
        case Search::Failed: return "Failed";
        case Search::External: return "External";
    }
    return "Closed";
}

enum class Comparison { Closed, Editing, Loading, Loaded, Failed };

inline std::string_view comparison_name(Comparison comparison) {
    switch (comparison) {
        case Comparison::Closed: return "Closed";
        case Comparison::Editing: return "Editing";
        case Comparison::Loading: return "Loading";
        case Comparison::Loaded: return "Loaded";
        case Comparison::Failed: return "Failed";
    }
    return "Closed";
}

// Range-diff lifecycle (fsm_todo.md M7): Idle -> Resolving (rev-parse of
// the four range endpoints in flight) -> Comparing (the range-diff read
// in flight) -> Done (display built) | Failed (error retained). `step`
// replaces the former `next` progress counter: the stage is named, not
// counted. All step writes go through the transition methods below;
// rendering reads `display`/`error`. Abandoning an in-flight resolution
// clears `resolved`, so a half-filled resolution never survives into
// the next open.
struct RangeDiff {
    enum class Step { Idle, Resolving, Comparing, Done, Failed };

    Step step = Step::Idle;
    bool enabled = false;
    std::string oldRange;
    std::string newRange;
    std::array<std::string, 4> revisions;
    std::array<std::string, 4> resolved;
    reading::RequestStamp requestStamp;
    async_work::Task<git::GitResult> future;
    std::vector<FileDiff> display;
    std::string error;

    bool busy() const { return step == Step::Resolving || step == Step::Comparing; }
    // Fresh submission: no resolved endpoints or display survive.
    void begin() {
        future = {};
        display.clear();
        error.clear();
        resolved = {};
        step = Step::Idle;
    }
    void begin_resolving(reading::RequestStamp stamp, std::array<std::string, 4> revs,
                         async_work::Task<git::GitResult> task) {
        requestStamp = std::move(stamp);
        revisions = std::move(revs);
        future = std::move(task);
        step = Step::Resolving;
    }
    void begin_comparing(async_work::Task<git::GitResult> task) {
        future = std::move(task);
        step = Step::Comparing;
    }
    void fail(std::string message) {
        error = std::move(message);
        future = {};
        step = Step::Failed;
    }
    void finish(std::vector<FileDiff> diff) {
        display = std::move(diff);
        future = {};
        step = Step::Done;
    }
    // An in-flight resolution was abandoned (panel closed, mode toggled,
    // stale result dropped): resolved endpoints do not carry over.
    void abandon() {
        if (!busy()) return;
        future = {};
        resolved = {};
        step = Step::Idle;
    }
    // The comparison document was left entirely.
    void reset() {
        future = {};
        resolved = {};
        display.clear();
        step = Step::Idle;
    }
};

inline std::string_view range_diff_step_name(RangeDiff::Step step) {
    switch (step) {
        case RangeDiff::Step::Idle: return "Idle";
        case RangeDiff::Step::Resolving: return "Resolving";
        case RangeDiff::Step::Comparing: return "Comparing";
        case RangeDiff::Step::Done: return "Done";
        case RangeDiff::Step::Failed: return "Failed";
    }
    return "Idle";
}

// A git log read plus its parsed commits, produced on a worker so the
// main thread never parses paging/scope results when they arrive.
struct ParsedCommitLog {
    git::GitResult raw;
    std::vector<CommitEntry> parsed;
};

// Refresh lifecycle (fsm_todo.md M1): NeverLoaded (no refresh has
// settled yet) -> Queued on request_refresh(scope) -> Running on
// begin_refresh() (the refresh system consuming the queued scope) ->
// Settled on finish_refresh() when the per-read futures drain. Settled
// -> Queued on the next request. The phase is the only refresh-request/
// running state: the former refreshRequested/isRefreshing/hasLoadedOnce
// bools are deleted, not wrapped. `refreshLoadedOnce_` is the monotonic
// latch Settled records — a re-refresh (Queued/Running after Settled)
// has still loaded once, a first load has not — and only the transition
// methods below write it, alongside the phase.
enum class RefreshPhase { NeverLoaded, Queued, Running, Settled };

inline std::string_view refresh_phase_name(RefreshPhase phase) {
    switch (phase) {
        case RefreshPhase::NeverLoaded: return "NeverLoaded";
        case RefreshPhase::Queued: return "Queued";
        case RefreshPhase::Running: return "Running";
        case RefreshPhase::Settled: return "Settled";
    }
    return "NeverLoaded";
}

// File-watcher lifecycle (fsm_todo.md M20): Idle (nothing watched
// yet) -> Swapping (a watch swap is in flight on a worker) ->
// Watching (draining events, nothing pending) -> CoolingDown (a
// scoped refresh is pending until the cooldown elapses and no
// refresh is running) -> Watching. Derived in
// FileWatcherSystem::phase(); the pending scope is an optional, so
// scope-without-pending is unrepresentable.
enum class Watch { Idle, Swapping, Watching, CoolingDown };

inline std::string_view watch_name(Watch watch) {
    switch (watch) {
        case Watch::Idle: return "Idle";
        case Watch::Swapping: return "Swapping";
        case Watch::Watching: return "Watching";
        case Watch::CoolingDown: return "CoolingDown";
    }
    return "Idle";
}

enum class LogPhase { Idle, LoadingScope, LoadingPage, Error };

inline std::string_view log_phase_name(LogPhase phase) {
    switch (phase) {
        case LogPhase::Idle: return "Idle";
        case LogPhase::LoadingScope: return "LoadingScope";
        case LogPhase::LoadingPage: return "LoadingPage";
        case LogPhase::Error: return "Error";
    }
    return "Idle";
}

// Computed once per completed read, from the returned count vs the
// requested page size — never re-inferred from the accumulated log size.
inline bool commit_log_has_more(size_t returned, size_t requested = git::kCommitLogPageSize) {
    return requested != 0 && returned >= requested;
}

struct CommitLogPage {
    LogPhase phase = LogPhase::Idle;
    bool hasMore = true;
    std::string error;
    std::string repository;
    std::string head;
    size_t offset = 0;
    size_t requestedPageSize = git::kCommitLogPageSize;
    async_work::Task<ParsedCommitLog> future;

    bool busy() const { return phase == LogPhase::LoadingScope || phase == LogPhase::LoadingPage; }
    void request_page() {
        if (busy()) return;
        error.clear();
        phase = LogPhase::LoadingPage;
    }
    void begin_scope() {
        error.clear();
        phase = LogPhase::LoadingScope;
    }
    void finish(size_t returned) {
        hasMore = commit_log_has_more(returned, requestedPageSize);
        error.clear();
        phase = LogPhase::Idle;
    }
    void fail(std::string message) {
        error = std::move(message);
        phase = LogPhase::Error;
    }
};

struct SourceFindMatch {
    int line = 1;
    int column = 1;
};

struct SelectionCopyResult {
    std::string text;
    std::string error;
    std::string revision;  // resolved commit, "INDEX", or empty for the working tree
    size_t maxPageBytes = 0;
};

struct SelectionCopyRuntime {
    async_slot::Slot<SelectionCopyResult> slot;
    reading::CodeSelection selection;
    bool withLocation = false;
    bool snippet = false;
};

struct SourceFindResult {
    std::vector<SourceFindMatch> matches;
    std::string sourceIdentity;
    std::string error;
    bool limited = false;
    uint64_t scannedBytes = 0;
    size_t maxPageBytes = 0;
};

struct OutlineResult {
    std::vector<symbol_outline::Symbol> symbols;
    std::string error;
};

struct ProjectSymbolEntry {
    std::string name, kind, path;
    int line = 1;
};

struct ProjectSymbolsResult {
    std::vector<ProjectSymbolEntry> symbols;
    std::string error;
};

// Source find scan (fsm_todo.md M12): the former key/request/future
// triple is one async_slot::Slot — request() stamps and launches,
// poll() drops stale results to Idle, cancel_scan() is the scan side
// of the unified find close. `result` stays the rendered data: the
// last landed scan for the slot's key, read exactly as before.
struct SourceFindRuntime {
    async_slot::Slot<SourceFindResult> slot;
    SourceFindResult result;

    bool busy() const { return slot.busy(); }
    // Cancel an in-flight scan; key and landed result are retained so
    // reopening the same query reuses them, as the old close did.
    void cancel_scan() { slot.cancel(); }
    void clear() {
        slot.invalidate();
        result = {};
    }
};

struct MarkdownLinkRuntime {
    async_slot::Slot<OutlineResult> slot;  // finds the anchor's heading
    reading::SourceLocation target;
    std::string anchor;
};

// Text outlines of files in the diff, for approximate hunk scopes. Keyed by
// scope, path, revision, and data generation; one scan runs at a time
// (fsm_todo.md M15). The single in-flight scan is one AsyncSlot; failures
// are cached in `failed` so a failed key renders as honest absence (no
// enclosing scope) instead of being relaunched every frame.
struct HunkScopeRuntime {
    std::map<std::string, std::vector<symbol_outline::Symbol>> outlines;
    std::set<std::string> failed;
    async_slot::Slot<OutlineResult> slot;
};

struct HunkContextResult {
    DiffHunk lines;
    std::string error;
    size_t bytes = 0;
};

struct HunkContextEntry {
    std::string version;
    HunkContextResult result;
};

// Identity of a hunk-context load (M15): `identity` keys the entries map
// (hunk key + above/below), `version` is the renderIdentity-derived
// source/range version the entry must match. One load at a time.
struct HunkContextKey {
    std::string identity;
    std::string version;
    bool operator==(const HunkContextKey&) const = default;
};

struct HunkContextRuntime {
    std::map<std::string, HunkContextEntry> entries;
    async_slot::Slot<HunkContextResult, HunkContextKey> slot;
};

struct DiffSyntaxResult {
    std::vector<std::pair<code_lexer::State, code_lexer::State>> seeds;
    std::string error;
};

// Diff syntax seeds for one scope (M15): the slot key is the file's
// renderIdentity; one read at a time per scope, and the unseeded-file
// fast path stays capped at 32 files per frame in ui::diff_syntax.
struct DiffSyntaxRuntime {
    async_slot::Slot<DiffSyntaxResult, std::uint64_t> slot;
};

// Adapters over a raw async_work::Task so not-yet-migrated machines can
// describe themselves to the slot registry (see fsm_todo.md Wave 0).
// `in_flight` is exactly frame_pacer::in_flight's predicate; `phase`
// cannot name Failed for a raw task (that needs consuming it with get()),
// so a ready-but-failed task reads Ready until its poll site consumes it.
template <class T>
inline bool task_in_flight(const async_work::Task<T>& task) {
    return task.valid() && task.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
}

template <class T>
inline std::string_view task_phase(const async_work::Task<T>& task) {
    if (!task.valid()) return async_slot::name(async_slot::Phase::Idle);
    return task.wait_for(std::chrono::seconds(0)) == std::future_status::ready
        ? async_slot::name(async_slot::Phase::Ready)
        : async_slot::name(async_slot::Phase::Pending);
}

// For machines the idle gate held on `future.valid()` alone: still busy
// once ready, until the poll site consumes the result.
template <class T>
inline std::string_view task_phase_until_consumed(const async_work::Task<T>& task) {
    return task.valid() ? async_slot::name(async_slot::Phase::Pending)
                        : async_slot::name(async_slot::Phase::Idle);
}

// Push dialog payload (M24): open state is LayoutComponent::modal ==
// PushDialog; this struct holds only the dialog's data.
struct PushDialogPayload {
    std::string repository;
    git::PushDestination destination;
    async_work::Task<git::PushDestination> future;
};

struct RepoComponent : public afterhours::BaseComponent {
    PushDialogPayload pushDialog;
    RangeDiff rangeDiff;
    commit_prefetch::State commitPrefetch;
    bool reviewWorkspace = false;
    review_files::Filter fileFilter;
    ReadingLayout reading;
    std::string readingSessionPath;
private:
    reading::ReadingWorkspace workspace_;
    friend struct ::navigation;
public:
    const reading::ReadingWorkspace& workspace() const { return workspace_; }
    std::optional<reading::NavigationEffect> navigationEffect;
    std::optional<reading::focus::Target> readingFocus;
    file_tree::NavigationState reviewTreeNavigation;
    file_tree::NavigationState filesTreeNavigation;
    std::vector<FileDiff> originFileSummaries;
    std::string repoPath;
    std::string currentBranch;
    bool isDirty = false;
    // Merge conflicts from the last status read.
    std::vector<std::string> conflictedFiles;
    bool mergeInProgress = false;
    bool isDetachedHead = false;
    std::string headCommitHash;
    int aheadCount = 0;
    int behindCount = 0;
    bool statusKnown = false;
    bool branchKnown = false;
    std::string statusError;

    std::vector<FileStatus> stagedFiles;
    std::vector<FileStatus> unstagedFiles;
    std::vector<std::string> untrackedFiles;
    std::vector<CommitEntry> commitLog;
    reading::HistorySelection historySelection;
    git::HistoryScope historyScope;
    struct HistoryPosition { reading::HistorySelection selection; std::string top; float fraction = 0.f; };
    std::map<std::string, HistoryPosition> historyPositions;
    bool historyScopeRequested = false;
    bool historyRestorePosition = false;
    async_work::Task<ParsedCommitLog> historyScopeFuture;
    std::string historyScopeRequestKey;
    std::string historyError;
    CommitLogPage commitLogPage;

    // Branch data (T031)
    std::vector<BranchInfo> branches;
    std::vector<std::string> tags;
    std::vector<std::string> stashes;

    std::vector<FileDiff> currentDiff;
    std::vector<FileDiff> stagedDiff;
    loading_feedback::Delay untrackedReviewLoading;
    UntrackedReviewRead untrackedReview;

    std::string cachedFilePath;
    std::string untrackedDiffKey;
    std::optional<FileDiff> untrackedDiff;
    // Per-file diff streamed ahead of the whole-repo diff (large repos):
    // keyed like untrackedDiff, filled by a single-file git diff.
    SelectedDiffRead selectedDiff;
    // dataGeneration at which each whole-repo diff was applied; empty
    // means it has not landed yet for the current state.
    std::optional<unsigned> currentDiffGeneration, stagedDiffGeneration;

    // Refresh lifecycle (M1, see RefreshPhase above). request_refresh()
    // is the one entry every refresh writer calls; the refresh system's
    // poll is the only other transition driver (begin/finish/requeue).
    RefreshPhase refreshPhase = RefreshPhase::NeverLoaded;
    // Scope queued for the next refresh; begin_refresh() consumes it
    // (None maps to Full, as the former flag code did). A request that
    // arrives while Running merges here, and finish_refresh() then
    // lands Queued instead of Settled so it runs next.
    refresh_scope::Scope refreshScope = refresh_scope::Scope::Full;
    std::string lastRefreshScope;
    bool refreshLoadedOnce_ = false;

    void request_refresh() {
        if (refreshPhase == RefreshPhase::Running) {
            // Queue a full re-refresh behind the running one.
            refreshScope = refresh_scope::Scope::Full;
            return;
        }
        refreshPhase = RefreshPhase::Queued;
    }
    void request_refresh(refresh_scope::Scope scope) {
        refreshScope = refresh_scope::merge(refreshScope, scope);
        if (refreshPhase != RefreshPhase::Running)
            refreshPhase = RefreshPhase::Queued;
    }
    bool refresh_requested() const { return refreshPhase == RefreshPhase::Queued; }
    bool refreshing() const { return refreshPhase == RefreshPhase::Running; }
    bool refresh_busy() const {
        return refreshPhase == RefreshPhase::Queued || refreshPhase == RefreshPhase::Running;
    }
    bool has_loaded_once() const { return refreshLoadedOnce_; }
    // Queued -> Running; consumes the queued scope (None -> Full).
    refresh_scope::Scope begin_refresh() {
        auto scope = refreshScope;
        if (scope == refresh_scope::Scope::None) scope = refresh_scope::Scope::Full;
        refreshScope = refresh_scope::Scope::None;
        refreshPhase = RefreshPhase::Running;
        return scope;
    }
    void note_refresh_loaded() { refreshLoadedOnce_ = true; }
    // Running -> Settled, or -> Queued when a request arrived while
    // Running (its scope is still queued in refreshScope).
    void finish_refresh() {
        refreshLoadedOnce_ = true;
        refreshPhase = refreshScope != refresh_scope::Scope::None
            ? RefreshPhase::Queued : RefreshPhase::Settled;
    }
    // Direct settle for code that loads synchronously (the e2e seed).
    void settle_refresh() {
        refreshLoadedOnce_ = true;
        refreshPhase = RefreshPhase::Settled;
    }
    // Aborted run (repository changed under it): drop back to Queued.
    void requeue_refresh() { refreshPhase = RefreshPhase::Queued; }
    // Run dropped with nothing queued (no pending reads, empty path).
    void abandon_refresh() {
        refreshPhase = refreshLoadedOnce_ ? RefreshPhase::Settled : RefreshPhase::NeverLoaded;
    }
    bool ignoreWhitespace = false;
    int diffContext = 3;
    std::optional<PageKey> fullFileHexPreviewKey;
    hex_view::Preview fullFileHexPreview;
    std::string fullFileDecodedText;
    markdown_preview::Cache fullFileMarkdownCache;
    MarkdownLinkRuntime markdownLink;
    bool fullFileMarkdownPreview = false;
    std::vector<FileDiff> fullFileDiff;
    // Rendered form of the active document's edit buffer (see source_edit.h);
    // rebuilt whenever Document::editGeneration moves past the rendered one.
    std::vector<FileDiff> editDiff;
    unsigned editRenderedGeneration = 0;
    bool editRendered = false;
    SourcePageWindow sourceWindow;
    // Paged full-file load (M5): phase, PageKey, page request, main +
    // revalidate tasks, stamp, spinner delay and error in one place.
    // Rendering still reads fullFileDiff/fullFilePage/sourceWindow below.
    FullFileLoad fullFileLoad;
    SourceFindRuntime sourceFind;
    std::uint64_t sourceFoldIdentity = 0;
    std::vector<source_folding::Range> sourceFoldRanges;
    SelectionCopyRuntime selectionCopy;
    HunkContextRuntime hunkContext;
    HunkScopeRuntime hunkScopes;
    std::map<std::string, DiffSyntaxRuntime> diffSyntax;
    reading_load::Trace fullFileTrace;
    FilePage fullFilePage;
    std::optional<reading::CodePosition> pendingCaret;
    // Language-server navigation (ui/semantic_navigation.h): one request at a
    // time, tied to the document and file state it was asked about.
    std::shared_ptr<semantic::Session> semanticSession;
    async_work::Task<semantic::Outcome> semanticFuture;
    std::uint64_t semanticDocument = 0;
    std::string semanticPath;
    std::string semanticSymbol;
    bool semanticEdited = false;
    std::optional<std::pair<std::filesystem::file_time_type, std::uintmax_t>> semanticStamp;
    // Hover/type info shown anchored to its token (cleared on caret move).
    std::string hoverInfo;
    reading::CodePosition hoverAnchor;
    std::optional<reading::CodeMotion> pendingCaretMotion;
    std::string fullFileEncodingOverride = "auto";
    std::string fullFileEncodingLabel;
    int fullFileRequestedTargetLine = 0;
    int fullFileRequestedTargetColumn = 0;
    unsigned repoVersion = 0;
    unsigned dataGeneration = 0;
    unsigned patchGeneration = 0;
    std::vector<std::string> allFilePaths;
    unsigned allFilePathsGeneration = 0;
    std::string filesError;
    std::string codeownersKey;
    async_work::Task<codeowners::Document> codeownersFuture;
    codeowners::Document codeownersDocument;
    std::unordered_map<std::string, codeowners::Document> codeownersCache;
    std::string codeownersFutureKey;
    std::unordered_map<std::string, std::string> codeownersByPath;
    Search repoSearchPhase = Search::Closed;
    bool repoSearchFocus = false;
    std::string repoSearchQuery;
    std::string repoSearchPath;
    std::string repoSearchError;
    bool repoSearchTruncated = false;
    size_t repoSearchCapturedBytes = 0;
    async_work::Task<SearchResult> repoSearchFuture;
    std::shared_ptr<SearchBatchQueue> repoSearchBatches;
    SearchQuery repoSearchScope;
    std::optional<reading::ReviewLocation> repoSearchOrigin;
    std::optional<reading::ReadingAnchor> repoSearchOriginAnchor;
    bool repoSearchChangesAvailable = false;
    std::uint64_t repoSearchGeneration = 0;
    std::uint64_t repoSearchRequestGeneration = 0;
    std::string repoSearchSubmittedQuery;
    std::optional<std::chrono::steady_clock::time_point> repoSearchDue;
    std::optional<size_t> repoSearchSelected;
    std::optional<size_t> repoSearchExpanded;
    float repoSearchScroll = 0.f;
    bool repoSearchRestoreScroll = true;
    std::string repoSearchSource;  // set when the list holds language-server results
    bool repoSearchOptionsOpen = false;
    bool repoSearchChangedOnly = false;
    SearchMatching repoSearchMatching;
    std::string repoSearchIncludeGlob;
    std::string repoSearchExcludeGlob;
    std::vector<SearchMatch> repoSearchResults;
    std::vector<SearchFileGroup> repoSearchGroups;
    std::vector<SearchResultRow> repoSearchRows;
    bool repoSearchRebuildRows = false;
    bool repoSearchPreviewOpen = false;
    async_work::Task<SearchPreview> repoSearchPreviewFuture;
    SearchPreview repoSearchPreview;
    // Excerpts already loaded, keyed by file/revision/line, so a re-search
    // reuses them for unchanged files instead of reading again.
    std::map<std::string, SearchPreview> repoSearchExcerptCache;
    int fullFileNavigateFrames = 0;
    bool fileHistoryOpen = false;
    std::string fileHistoryPath;
    std::string fileHistoryRevision;
    int fileHistoryLimit = 200;
    async_slot::Slot<git::GitResult> fileHistory;
    std::vector<CommitEntry> fileHistoryEntries;
    // Blame, line + gutter flavours (fsm_todo.md M13): one AsyncSlot
    // each. `blame` is keyed by the blamed line, `blameGutter` by
    // path+revision+generation so a file changed while unfocused
    // re-blames on the next refresh. Rendering still reads blameLine /
    // blameGutterLines; errors live on the slots (slot.error, Failed).
    async_slot::Slot<git::GitResult> blame;
    BlameLine blameLine;
    bool blameOpen = false;
    async_slot::Slot<git::GitResult> blameGutter;
    std::vector<BlameLine> blameGutterLines;
    bool commitSearchOpen = false;
    git::HistoryQuery commitSearchQuery;
    int commitSearchLimit = 200;
    async_slot::Slot<git::GitResult> commitSearch;
    std::vector<CommitEntry> commitSearchEntries;
    Comparison comparison = Comparison::Closed;
    bool comparisonNeedsLoad = false;
    std::string comparisonBase;
    std::string comparisonTarget;
    bool comparisonMergeBase = false;
    async_work::Task<git::RevisionComparison> comparisonFuture;
    loading_feedback::Delay comparisonLoading;
    std::vector<FileDiff> comparisonDiff;
    std::string comparisonLoadedScope;
    reading::RequestStamp comparisonRequestStamp;
    enum class ComparisonRequest { Document, SubmittedForm };
    ComparisonRequest comparisonRequest = ComparisonRequest::Document;
    std::string comparisonError;
    async_slot::Slot<git::GitResult> reviewQueue;
    int comparisonContext = 3;
    bool comparisonIgnoreWhitespace = false;
    int diffTargetFrames = 0;

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
    int fullFileTargetLine() const { return workspace_.recent_source() ? workspace_.recent_source()->line : 0; }
    const std::string& fullFilePath() const {
        static const std::string empty;
        return workspace_.recent_source() ? workspace_.recent_source()->destination.path : empty;
    }
    const std::string& fullFileRevision() const {
        static const std::string empty;
        return workspace_.recent_source() ? reading::revision_text(workspace_.recent_source()->destination.revision) : empty;
    }
    const std::string comparisonScope() const {
        return std::holds_alternative<reading::ComparisonReview>(workspace_.review().destination)
            ? reading::scope(workspace_.review()) : "";
    }
    // The stored phase is the only comparison open state (M7): the
    // former derivation from the workspace destination plus the
    // ComparisonEditor modal (two sources of truth) is deleted.
    bool comparisonOpen() const { return comparison != Comparison::Closed; }
    void open_comparison_editor() { comparison = Comparison::Editing; }
    void begin_comparison_load() { comparison = Comparison::Loading; }
    void finish_comparison_load() { comparison = Comparison::Loaded; }
    void fail_comparison(std::string message) {
        comparisonError = std::move(message);
        comparison = Comparison::Failed;
    }
    void close_comparison() { comparison = Comparison::Closed; }
    // A load was cancelled without leaving the panel (mode toggle):
    // fall back to editing the form instead of spinning in Loading.
    void cancel_comparison_load() {
        comparisonFuture = {};
        if (comparison == Comparison::Loading) comparison = Comparison::Editing;
    }
    // Navigation and editor-dismiss transition: the active document is
    // the new truth — a comparison document is Loading while its read
    // is owed or in flight, Loaded once its scope is the loaded scope,
    // Failed while an error is retained; anything else (working-tree
    // review, source view, another review) closes the comparison.
    void sync_comparison_phase() {
        if (workspace_.active() != reading::Slot::Review || comparisonScope().empty())
            comparison = Comparison::Closed;
        else if (comparisonFuture.valid() || comparisonNeedsLoad || comparisonLoadedScope != comparisonScope())
            comparison = Comparison::Loading;
        else if (!comparisonError.empty())
            comparison = Comparison::Failed;
        else
            comparison = Comparison::Loaded;
    }

    // Repository search transitions (fsm_todo.md M10): the only writers
    // of repoSearchPhase. The submitted triple (query + include/exclude
    // globs, the latter kept on repoSearchScope) is what the panel's
    // per-frame edit check compares the live text against, so injected
    // External results need no fake observed text: show_search_external()
    // records the triple as part of the transition.
    bool repo_search_open() const { return repoSearchPhase != Search::Closed; }
    void note_search_submitted() {
        repoSearchSubmittedQuery = repoSearchQuery;
        repoSearchScope.includeGlob = repoSearchIncludeGlob;
        repoSearchScope.excludeGlob = repoSearchExcludeGlob;
    }
    bool search_text_changed() const {
        return repoSearchQuery != repoSearchSubmittedQuery ||
               repoSearchIncludeGlob != repoSearchScope.includeGlob ||
               repoSearchExcludeGlob != repoSearchScope.excludeGlob;
    }
    void open_search() {
        if (repoSearchPhase != Search::Closed) return;
        repoSearchPhase = repoSearchDue ? Search::Debouncing
                          : !repoSearchSource.empty() ? Search::External
                                                      : Search::Done;
    }
    void close_search() { repoSearchPhase = Search::Closed; }
    void debounce_search(std::chrono::steady_clock::time_point due) {
        note_search_submitted();
        if (repoSearchQuery.empty()) {
            repoSearchDue.reset();
            repoSearchPhase = Search::Done;
        } else {
            repoSearchDue = due;
            repoSearchPhase = Search::Debouncing;
        }
    }
    void begin_search() {
        note_search_submitted();
        repoSearchDue.reset();
        repoSearchPhase = Search::Running;
    }
    void finish_search() {
        repoSearchPhase = repoSearchError.empty() ? Search::Done : Search::Failed;
    }
    void fail_search(std::string message) {
        repoSearchError = std::move(message);
        repoSearchPhase = Search::Failed;
    }
    void show_search_external(std::string source) {
        note_search_submitted();
        repoSearchDue.reset();
        repoSearchSource = std::move(source);
        repoSearchPhase = Search::External;
    }

    // Per-component slot registry (fsm_todo.md Wave 0): every async machine
    // on this component self-describes as {name, busy(), phase()} over its
    // existing fields — no state is wrapped or moved yet, so readers keep
    // reading the same fields. `gates_idle` marks exactly the entries the
    // e2e wait_for_refresh gate held on before this registry existed; the
    // gate iterates those and no others, so its behaviour is unchanged.
    struct AsyncSlotInfo {
        std::string_view name;
        bool (*busy)(const RepoComponent&);
        std::string_view (*phase)(const RepoComponent&);
        bool gates_idle = false;
    };

    std::span<const AsyncSlotInfo> async_slots() const {
        static constexpr AsyncSlotInfo slots[] = {
            {"refresh",
             [](const RepoComponent& r) { return r.refresh_busy(); },
             [](const RepoComponent& r) { return refresh_phase_name(r.refreshPhase); },
             true},
            {"untracked_review",
             [](const RepoComponent& r) {
                 return !r.untrackedFiles.empty() &&
                     !r.untrackedReview.current_for(r.dataGeneration, r.repoPath);
             },
             [](const RepoComponent& r) { return untracked_review_phase_name(r.untrackedReview.phase); },
             true},
            {"full_file",
             [](const RepoComponent& r) { return !r.fullFilePath().empty() && r.fullFileLoad.task_in_flight(); },
             [](const RepoComponent& r) { return file_load_name(r.fullFileLoad.phase); },
             true},
            {"selection_copy",
             [](const RepoComponent& r) { return r.selectionCopy.slot.busy() && task_in_flight(r.selectionCopy.slot.task); },
             [](const RepoComponent& r) {
                 return r.selectionCopy.slot.busy() && r.selectionCopy.slot.ready()
                     ? async_slot::name(async_slot::Phase::Ready) : async_slot::name(r.selectionCopy.slot.phase);
             },
             true},
            {"source_find",
             [](const RepoComponent& r) { return r.sourceFind.busy(); },
             [](const RepoComponent& r) { return async_slot::name(r.sourceFind.slot.phase); },
             true},
            {"diff_syntax",
             [](const RepoComponent& r) {
                 return std::any_of(r.diffSyntax.begin(), r.diffSyntax.end(),
                     [](const auto& entry) { return entry.second.slot.busy() && task_in_flight(entry.second.slot.task); });
             },
             [](const RepoComponent& r) {
                 bool anyReady = false, anyFailed = false;
                 for (const auto& entry : r.diffSyntax) {
                     if (entry.second.slot.busy() && task_in_flight(entry.second.slot.task)) return async_slot::name(async_slot::Phase::Pending);
                     if (entry.second.slot.phase == async_slot::Phase::Failed) anyFailed = true;
                     if (entry.second.slot.phase == async_slot::Phase::Ready || entry.second.slot.ready()) anyReady = true;
                 }
                 if (anyFailed) return async_slot::name(async_slot::Phase::Failed);
                 return anyReady ? async_slot::name(async_slot::Phase::Ready)
                                 : async_slot::name(async_slot::Phase::Idle);
             },
             true},
            {"hunk_scopes",
             [](const RepoComponent& r) { return r.hunkScopes.slot.busy() && task_in_flight(r.hunkScopes.slot.task); },
             [](const RepoComponent& r) {
                 return r.hunkScopes.slot.busy() && r.hunkScopes.slot.ready()
                     ? async_slot::name(async_slot::Phase::Ready) : async_slot::name(r.hunkScopes.slot.phase);
             },
             true},
            {"hunk_context",
             [](const RepoComponent& r) { return r.hunkContext.slot.busy() && task_in_flight(r.hunkContext.slot.task); },
             [](const RepoComponent& r) {
                 return r.hunkContext.slot.busy() && r.hunkContext.slot.ready()
                     ? async_slot::name(async_slot::Phase::Ready) : async_slot::name(r.hunkContext.slot.phase);
             },
             true},
            {"repo_search",
             [](const RepoComponent& r) {
                 return r.repoSearchPhase == Search::Debouncing || r.repoSearchPhase == Search::Running;
             },
             [](const RepoComponent& r) { return search_name(r.repoSearchPhase); },
             true},
            {"repo_search_preview",
             [](const RepoComponent& r) { return task_in_flight(r.repoSearchPreviewFuture); },
             [](const RepoComponent& r) { return task_phase(r.repoSearchPreviewFuture); },
             true},
            {"codeowners",
             [](const RepoComponent& r) { return task_in_flight(r.codeownersFuture); },
             [](const RepoComponent& r) { return task_phase(r.codeownersFuture); },
             true},
            {"file_history",
             [](const RepoComponent& r) { return r.fileHistory.busy(); },
             [](const RepoComponent& r) { return async_slot::name(r.fileHistory.phase); },
             true},
            {"blame",
             [](const RepoComponent& r) { return r.blame.busy() && task_in_flight(r.blame.task); },
             [](const RepoComponent& r) {
                 return r.blame.busy() && r.blame.ready() ? async_slot::name(async_slot::Phase::Ready)
                                                          : async_slot::name(r.blame.phase);
             },
             true},
            {"commit_search",
             [](const RepoComponent& r) { return r.commitSearch.busy(); },
             [](const RepoComponent& r) { return async_slot::name(r.commitSearch.phase); },
             true},
            {"comparison",
             [](const RepoComponent& r) { return task_in_flight(r.comparisonFuture); },
             [](const RepoComponent& r) { return comparison_name(r.comparison); },
             true},
            {"review_queue",
             [](const RepoComponent& r) { return r.reviewQueue.busy(); },
             [](const RepoComponent& r) { return async_slot::name(r.reviewQueue.phase); },
             true},
            {"range_diff",
             [](const RepoComponent& r) { return r.rangeDiff.busy(); },
             [](const RepoComponent& r) { return range_diff_step_name(r.rangeDiff.step); },
             true},
            {"commit_log_page",
             [](const RepoComponent& r) { return r.commitLogPage.busy() || r.commitLogPage.future.valid(); },
             [](const RepoComponent& r) { return log_phase_name(r.commitLogPage.phase); },
             true},
            {"history_scope",
             [](const RepoComponent& r) { return r.historyScopeRequested || r.historyScopeFuture.valid() || r.commitLogPage.phase == LogPhase::LoadingScope; },
             [](const RepoComponent& r) {
                 if (r.commitLogPage.phase == LogPhase::LoadingScope) return log_phase_name(LogPhase::LoadingScope);
                 return (r.historyScopeRequested || r.historyScopeFuture.valid())
                     ? log_phase_name(LogPhase::LoadingScope)
                     : log_phase_name(r.commitLogPage.phase);
             },
             true},
            {"push_destination",
             [](const RepoComponent& r) { return r.pushDialog.future.valid(); },
             [](const RepoComponent& r) { return task_phase_until_consumed(r.pushDialog.future); },
             true},
            {"selected_diff",
             [](const RepoComponent& r) { return r.selectedDiff.pending(); },
             [](const RepoComponent& r) { return diff_source_name(r.selectedDiff.source); },
             false},
            {"semantic",
             [](const RepoComponent& r) {
                 return task_in_flight(r.semanticFuture) ||
                     (r.semanticSession && (r.semanticSession->lsp_state() == semantic::Lsp::Busy ||
                                            r.semanticSession->lsp_state() == semantic::Lsp::Starting));
             },
             [](const RepoComponent& r) {
                 return r.semanticSession ? semantic::lsp_name(r.semanticSession->lsp_state())
                                          : semantic::lsp_name(semantic::Lsp::Unstarted);
             },
             false},
            {"markdown_link",
             [](const RepoComponent& r) { return r.markdownLink.slot.busy() && task_in_flight(r.markdownLink.slot.task); },
             [](const RepoComponent& r) {
                 return r.markdownLink.slot.busy() && r.markdownLink.slot.ready()
                     ? async_slot::name(async_slot::Phase::Ready) : async_slot::name(r.markdownLink.slot.phase);
             },
             false},
            {"full_file_revalidate",
             [](const RepoComponent& r) { return r.fullFileLoad.revalidate_in_flight(); },
             [](const RepoComponent& r) {
                 return r.fullFileLoad.revalidating() ? file_load_name(FileLoad::Revalidating)
                     : r.fullFileLoad.revalidate.valid() ? async_slot::name(async_slot::Phase::Pending)
                     : async_slot::name(async_slot::Phase::Idle);
             },
             false},
            {"blame_gutter",
             [](const RepoComponent& r) { return r.blameGutter.busy() && task_in_flight(r.blameGutter.task); },
             [](const RepoComponent& r) {
                 return r.blameGutter.busy() && r.blameGutter.ready() ? async_slot::name(async_slot::Phase::Ready)
                                                                      : async_slot::name(r.blameGutter.phase);
             },
             false},
        };
        return slots;
    }

    const AsyncSlotInfo* find_async_slot(std::string_view name) const {
        for (const auto& slot : async_slots())
            if (slot.name == name) return &slot;
        return nullptr;
    }

    std::optional<std::string_view> async_slot_phase(std::string_view name) const {
        const auto* slot = find_async_slot(name);
        return slot ? std::optional<std::string_view>(slot->phase(*this)) : std::nullopt;
    }

    bool async_idle() const {
        for (const auto& slot : async_slots())
            if (slot.gates_idle && slot.busy(*this)) return false;
        return true;
    }
};

inline bool source_tab_active(const RepoComponent& repo) {
    return repo.workspace().active() == reading::Slot::Source && !repo.fullFilePath().empty();
}

inline void cancel_hidden_file_read(RepoComponent& repo) {
    if (repo.fullFilePath().empty() && repo.fullFileLoad.task.valid()) repo.fullFileLoad.reset();
}

inline std::string selected_commit_parent(const RepoComponent& repo) {
    const auto* commit = std::get_if<reading::CommitReview>(&repo.workspace().review().destination);
    return commit && commit->parent ? reading::revision_text(*commit->parent) : "";
}

inline std::string commit_review_scope(const RepoComponent& repo) {
    return reading::scope(repo.workspace().review());
}

struct CommitDetailRuntime {
    CommitEntry entry;
    async_slot::Slot<CommitPatch, PatchKey> patch;
    // The slot's task is validating a patch already shown from the cache
    // peek; its result replaces what is shown, so a missing commit still
    // ends as an error. While true this is not a cold load: no spinner,
    // and readers keep using the peeked diff.
    bool patchRevalidating = false;
    loading_feedback::Delay loading;
    reading_load::Trace trace;
    // A cold load is in flight (the peek-revalidate task does not count).
    bool patch_loading() const { return patch.task.valid() && !patchRevalidating; }
    std::vector<FileDiff> commitDetailDiff;
    std::string commitDetailBody;
    float messageWrapWidth = 0.f;
    float messageFontSize = 0.f;
    std::vector<std::string> messageLines;
    size_t messageVisibleRows = 0;
    std::string commitDetailAuthorEmail;
    std::string commitDetailParents;
    std::string commitDetailError;
    bool fileOverviewExpanded = false;
    bool foldDefaultsApplied = false;
};

struct CommitDetailCache : public afterhours::BaseComponent, CommitDetailRuntime {};

// Review session-store lifecycle (fsm_todo.md M17). One phase owns the
// load/save state that used to be `dirty` + `loadError` truthiness plus the
// `nextSaveAttempt` retry loop: Unloaded -> Loading -> Ready, mutations ->
// Dirty, persist -> Saving -> Ready | SaveFailed (unsaved work retained),
// load failure -> LoadFailed (saves blocked until a successful retry_load).
enum class ReviewStorePhase { Unloaded, Loading, Ready, Dirty, Saving, SaveFailed, LoadFailed };

// Review baseline lifecycle (fsm_todo.md M19): None (no snapshot) ->
// Capturing (capture task in flight) -> Ready (snapshot held), Failed
// (last capture/compare failed; any held snapshot is retained). The
// snapshot path itself is `baselineSnapshot` as optional<string>; the
// former snapshotCapturing bool (which duplicated the capture task) is
// deleted — Capturing is exactly a capture in flight, and a compare in
// flight leaves the phase at Ready/Failed until poll resolves it.
enum class Baseline { None, Capturing, Ready, Failed };

inline std::string_view baseline_name(Baseline baseline) {
    switch (baseline) {
        case Baseline::None: return "None";
        case Baseline::Capturing: return "Capturing";
        case Baseline::Ready: return "Ready";
        case Baseline::Failed: return "Failed";
    }
    return "None";
}

// Comment composer lifecycle (fsm_todo.md M18): Closed -> Composing
// (new comment on a hunk, target is the HunkKey) or EditingExisting
// (editing a saved comment, target is its CommentIndex) -> Closed via
// commit/dismiss. The 12 composing fields and the editingComment trio
// are deleted; the shared text/kind/location buffer is `composerDraft`
// (a Comment) and `composerTarget` says what it is aimed at.
// begin_comment/dismiss_pending_comment/commit_pending_comment below
// are the sole phase mutators; rendering reads composer/composerDraft.
enum class Composer { Closed, Composing, EditingExisting };

inline std::string_view composer_name(Composer composer) {
    switch (composer) {
        case Composer::Closed: return "Closed";
        case Composer::Composing: return "Composing";
        case Composer::EditingExisting: return "EditingExisting";
    }
    return "Closed";
}

inline std::string_view review_store_phase_name(ReviewStorePhase phase) {
    switch (phase) {
        case ReviewStorePhase::Unloaded: return "Unloaded";
        case ReviewStorePhase::Loading: return "Loading";
        case ReviewStorePhase::Ready: return "Ready";
        case ReviewStorePhase::Dirty: return "Dirty";
        case ReviewStorePhase::Saving: return "Saving";
        case ReviewStorePhase::SaveFailed: return "SaveFailed";
        case ReviewStorePhase::LoadFailed: return "LoadFailed";
    }
    return "Unloaded";
}

// Per-tab "Ballroom" review state (see docs/mocks/ballroom.html).
struct ReviewComponent : public afterhours::BaseComponent {
    ReviewStorePhase storePhase = ReviewStorePhase::Unloaded;
    std::string storeError;
    struct FeedbackRange {
        int first = 0, last = 0;
        bool oldSide = false;
        bool operator==(const FeedbackRange&) const = default;
    };
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
        std::vector<FeedbackRange> ranges;
    };
    bool reviewing = false;
    bool basketOpen = true;   // feedback basket panel shown (toggle in diff header)
    bool showApproved = false;
    bool showResolved = false;
    std::vector<Comment> comments;
    // Composer state (M18, see Composer above). `composerTarget` holds a
    // HunkKey while Composing and a CommentIndex while EditingExisting;
    // `composerDraft` is the shared edit buffer (for EditingExisting it
    // is a copy of the target comment; only text/kind are written back).
    using HunkKey = std::string;
    using CommentIndex = int;
    Composer composer = Composer::Closed;
    std::variant<HunkKey, CommentIndex> composerTarget;
    Comment composerDraft;
    bool composerFocus = false;
    const HunkKey& composing_key() const {
        static const HunkKey empty;
        return composer == Composer::Composing ? std::get<HunkKey>(composerTarget) : empty;
    }
    CommentIndex editing_index() const {
        return composer == Composer::EditingExisting ? std::get<CommentIndex>(composerTarget) : -1;
    }
    // Deleting a saved comment shifts/closes an EditingExisting target.
    void on_comment_erased(size_t index) {
        if (composer != Composer::EditingExisting) return;
        const CommentIndex target = editing_index();
        if (target == static_cast<CommentIndex>(index)) {
            composer = Composer::Closed;
            composerTarget = HunkKey{};
            composerDraft = {};
            composerFocus = false;
        } else if (target > static_cast<CommentIndex>(index)) {
            composerTarget = target - 1;
        }
    }
    std::map<std::string, Comment> drafts;
    std::set<std::string> approvedHunks;
    std::map<std::string, std::string> reviewedFiles;
    std::map<std::string, ReviewDecision> verdicts;
    CommitReviewQueue queue;
    std::set<std::string> foldedHunks;
    std::set<std::string> foldedFiles;
    // Keyboard chunk cursor (vim-style j/k/n nav; a approve, c comment).
    int cursor = 0;              // index of the highlighted visible hunk
    bool cursorMoved = false;
    int hunkCount = 0;           // visible hunks last frame (for clamping)
    bool cursorApprove = false;  // request: approve the cursor hunk
    bool cursorComment = false;  // request: comment on the cursor hunk
    // Durable-state mutations call mark_dirty(); MainContentSystem drains
    // Dirty/SaveFailed via review_store::persist_review. Not serialized.
    bool dirty() const {
        return storePhase == ReviewStorePhase::Dirty || storePhase == ReviewStorePhase::SaveFailed;
    }
    void mark_dirty() {
        // A failed load/save already denotes unsaved work; keep that
        // failure visible until a retry succeeds instead of masking it.
        if (storePhase == ReviewStorePhase::LoadFailed ||
            storePhase == ReviewStorePhase::SaveFailed ||
            storePhase == ReviewStorePhase::Saving) return;
        storePhase = ReviewStorePhase::Dirty;
    }
    void begin_load() { storePhase = ReviewStorePhase::Loading; storeError.clear(); }
    void finish_load() { storePhase = ReviewStorePhase::Ready; storeError.clear(); }
    void fail_load(std::string error) {
        storePhase = ReviewStorePhase::LoadFailed;
        storeError = std::move(error);
    }
    void begin_save() { storePhase = ReviewStorePhase::Saving; }
    void finish_save() {
        storePhase = ReviewStorePhase::Ready;
        storeError.clear();
        nextSaveAttempt = {};
    }
    void fail_save(std::string error) {
        storePhase = ReviewStorePhase::SaveFailed;
        storeError = std::move(error);
    }
    std::chrono::steady_clock::time_point nextSaveAttempt{};
    std::string storageScope;
    std::string storageRepoPath;
    // Async review-file load (review_store::load_review_async).
    async_work::Task<ReviewComponent> reviewLoadFuture;
    std::string reviewLoadScope;
    std::string reviewLoadRepoPath;
    // "New since you last looked": diff signature of each file when last viewed.
    std::map<std::string, std::string> seenSig;
    // Baseline snapshot for "new since you last looked" (Phase 6, M19).
    Baseline baseline = Baseline::None;
    std::string baselineHead;     // HEAD sha captured on Embark
    std::string baselineDiffSig;  // signature of the working diff on Embark
    std::optional<std::string> baselineSnapshot;
    int64_t baselineCapturedAt = 0;
    async_work::Task<git::GitResult> snapshotFuture;
    int snapshotContext = 3;
    bool snapshotIgnoreWhitespace = false;
    bool sinceReviewOpen = false;
    std::vector<FileDiff> sinceReviewDiff;
    std::string snapshotError;
    // Baseline transitions (M19); review_snapshot.h start/poll are the
    // callers. A failed capture/compare keeps any held snapshot.
    void begin_baseline_capture() { baseline = Baseline::Capturing; snapshotError.clear(); }
    void finish_baseline_capture(std::string path, std::string head, int64_t capturedAt) {
        baselineSnapshot = std::move(path);
        baselineHead = std::move(head);
        baselineCapturedAt = capturedAt;
        baseline = Baseline::Ready;
    }
    void finish_baseline_compare() { if (baselineSnapshot) baseline = Baseline::Ready; }
    void fail_baseline(std::string error) {
        snapshotError = std::move(error);
        baseline = Baseline::Failed;
    }

    static std::string hunk_key(const std::string& filePath,
                                const DiffHunk& hunk) {
        return filePath + "\n" + hunk_signature(hunk);
    }
};

inline void reset_review(ReviewComponent& review) {
    review.storePhase = ReviewStorePhase::Unloaded;
    review.storeError.clear();
    review.reviewing = false;
    review.basketOpen = true;
    review.showApproved = false;
    review.showResolved = false;
    review.comments.clear();
    review.composer = Composer::Closed;
    review.composerTarget = ReviewComponent::HunkKey{};
    review.composerDraft = {};
    review.composerFocus = false;
    review.drafts.clear();
    review.nextSaveAttempt = {};
    review.storageScope.clear();
    review.storageRepoPath.clear();
    review.approvedHunks.clear();
    review.reviewedFiles.clear();
    review.verdicts.clear();
    review.queue = {};
    review.foldedHunks.clear();
    review.foldedFiles.clear();
    review.cursor = 0;
    review.cursorMoved = false;
    review.hunkCount = 0;
    review.cursorApprove = false;
    review.cursorComment = false;
    review.seenSig.clear();
    review.baseline = Baseline::None;
    review.baselineHead.clear();
    review.baselineDiffSig.clear();
    review.baselineSnapshot.reset();
    review.baselineCapturedAt = 0;
    review.snapshotFuture = {};
    review.sinceReviewOpen = false;
    review.sinceReviewDiff.clear();
    review.snapshotError.clear();
}

inline std::string review_scope(const RepoComponent& repo) {
    return (repo.isDetachedHead ? "detached" : "branch:" + repo.currentBranch) +
        "\nrevision:" + (repo.headCommitHash.empty() ? "unborn" : repo.headCommitHash);
}

inline std::string selected_review_storage_scope(const RepoComponent& repo, const ReviewComponent& review, bool comparisonEditorOpen = false) {
    if (!repo.reviewQueue.key.empty()) return repo.reviewQueue.key;
    if (!repo.comparisonScope().empty()) return repo.comparisonScope();
    if (comparisonEditorOpen) {
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

template<class Signature, class HunkKey>
inline bool file_reviewed(const ReviewComponent& review, const std::string& scope, const FileDiff& file, Signature signature, HunkKey hunkKey) {
    if (file.isPartialContent) return false;
    auto record = review.reviewedFiles.find(scope + "\n" + file.filePath);
    if ((file.oldMode != file.newMode || !file.oldPath.empty()) &&
        (record == review.reviewedFiles.end() || record->second != signature(file))) return false;
    if (!file.hunks.empty())
        return std::all_of(file.hunks.begin(), file.hunks.end(), [&](const auto& hunk) {
            return review.approvedHunks.contains(scope + "\n" + hunkKey(file, hunk));
        });
    return record != review.reviewedFiles.end() && record->second == signature(file);
}

inline bool file_reviewed(const ReviewComponent& review, const std::string& scope, const FileDiff& file) {
    return file_reviewed(review, scope, file, diff_signature,
        [](const FileDiff& value, const DiffHunk& hunk) { return ReviewComponent::hunk_key(value.filePath, hunk); });
}

inline bool file_reviewed(const ReviewComponent& review, const std::string& scope, const reading::FileSummary& file) {
    if (file.partial) return false;
    auto record = review.reviewedFiles.find(scope + "\n" + file.path);
    bool recorded = record != review.reviewedFiles.end() && record->second == file.signature;
    if (file.requiresFileRecord && !recorded) return false;
    return file.hunkKeys.empty() ? recorded : std::all_of(file.hunkKeys.begin(), file.hunkKeys.end(),
        [&](const auto& key) { return review.approvedHunks.contains(scope + "\n" + key); });
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
    if (!comment.ranges.empty()) {
        std::string location = comment.file + ": ";
        for (const auto& range : comment.ranges) {
            if (location.back() != ' ') location += ", ";
            location += std::string(range.oldSide ? "before " : "after ") + std::to_string(range.first);
            if (range.last > range.first) location += "-" + std::to_string(range.last);
        }
        return location;
    }

    auto out = comment.file + ":" + std::to_string(comment.line);
    if (comment.endLine > comment.line) out += "-" + std::to_string(comment.endLine);
    if (comment.oldSide) out += " (old)";
    return out;
}

// Composer transitions (defined below pending_comment); they are the
// sole mutators of composer/composerTarget/composerDraft phases.
inline void begin_comment(ReviewComponent& review, const std::string& key,
                          ReviewComponent::Comment location);
inline void begin_comment(ReviewComponent& review, ReviewComponent::CommentIndex index);
inline void dismiss_pending_comment(ReviewComponent& review);
inline void commit_pending_comment(ReviewComponent& review);
inline bool save_comment_edit(ReviewComponent& review);

inline void erase_comment(ReviewComponent& review, size_t index) {
    const auto removed = review.comments.at(index);
    review.comments.erase(review.comments.begin() + static_cast<std::ptrdiff_t>(index));
    review.on_comment_erased(index);
    if (std::none_of(review.comments.begin(), review.comments.end(), [&](const auto& comment) {
            return comment.scope == removed.scope && comment.file == removed.file;
        })) {
        auto prefix = removed.scope + "\n" + removed.file + "\n";
        std::erase_if(review.foldedHunks, [&](const auto& key) { return key.starts_with(prefix); });
    }
    review.mark_dirty();
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

template<class File, class Reviewed>
inline ReviewProgress review_progress(const ReviewComponent& review, const std::string& scope,
        const std::vector<File>& files, Reviewed reviewed) {
    ReviewProgress progress;
    progress.total = files.size();
    progress.reviewed = static_cast<size_t>(std::count_if(files.begin(), files.end(),
        [&](const auto& file) { return reviewed(review, scope, file); }));
    progress.unresolved = static_cast<size_t>(std::count_if(review.comments.begin(), review.comments.end(),
        [&](const auto& comment) { return comment.scope == scope && !comment.resolved; }));
    return progress;
}

template<class File>
inline ReviewProgress review_progress(const ReviewComponent& review, const std::string& scope,
        const std::vector<File>& files) {
    return review_progress(review, scope, files,
        [](const auto& value, const auto& key, const auto& file) { return file_reviewed(value, key, file); });
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
    if (cache.patch.key.commit != repo.selectedCommitHash() || !cache.patch.key.parent.empty()) return false;
    if (cache.patch_loading() || !cache.commitDetailError.empty()) return false;
    return current_review_verdict(review, repo.selectedCommitHash(), cache.commitDetailDiff) == ReviewVerdict::InProgress;
}

inline ReviewComponent::Comment pending_comment(const ReviewComponent& review) {
    return review.composerDraft;
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

namespace composer_detail {
inline void close_composer(ReviewComponent& review) {
    review.composer = Composer::Closed;
    review.composerTarget = ReviewComponent::HunkKey{};
    review.composerDraft = {};
    review.composerFocus = false;
}
// Draft-on-dismiss: a Composing buffer is kept in `drafts` (or dropped
// when empty); an EditingExisting buffer is simply discarded, leaving
// the saved comment untouched (the old basket Cancel behaviour).
inline void preserve_composing_draft(ReviewComponent& review) {
    if (review.composer != Composer::Composing) return;
    if (review.composerDraft.text.empty()) review.drafts.erase(review.composing_key());
    else review.drafts[review.composing_key()] = pending_comment(review);
}
}  // namespace composer_detail

inline void dismiss_pending_comment(ReviewComponent& review) {
    if (review.composer == Composer::Closed) return;
    composer_detail::preserve_composing_draft(review);
    composer_detail::close_composer(review);
    review.mark_dirty();
}

inline void begin_comment(ReviewComponent& review, const std::string& key,
                           ReviewComponent::Comment location) {
    composer_detail::preserve_composing_draft(review);
    if (auto draft = review.drafts.find(key); draft != review.drafts.end()) location = draft->second;
    review.composer = Composer::Composing;
    review.composerTarget = key;
    review.composerDraft = std::move(location);
    review.composerDraft.resolved = false;
    review.composerFocus = true;
    review.mark_dirty();
}

inline void begin_comment(ReviewComponent& review, ReviewComponent::CommentIndex index) {
    if (index < 0 || static_cast<size_t>(index) >= review.comments.size()) return;
    composer_detail::preserve_composing_draft(review);
    review.composer = Composer::EditingExisting;
    review.composerTarget = index;
    review.composerDraft = review.comments[static_cast<size_t>(index)];
    review.composerFocus = false;
    review.mark_dirty();
}

inline void commit_pending_comment(ReviewComponent& r) {
    if (r.composer == Composer::Composing) {
        if (!r.composerDraft.text.empty()) {
            r.comments.push_back(pending_comment(r));
            r.mark_dirty();
        }
        r.drafts.erase(r.composing_key());
        r.mark_dirty();
        composer_detail::close_composer(r);
    } else if (r.composer == Composer::EditingExisting) {
        const auto index = r.editing_index();
        if (index < 0 || static_cast<size_t>(index) >= r.comments.size() ||
            r.composerDraft.text.empty()) return;
        r.comments[static_cast<size_t>(index)].text = r.composerDraft.text;
        r.comments[static_cast<size_t>(index)].kind = r.composerDraft.kind;
        r.mark_dirty();
        composer_detail::close_composer(r);
    }
}

inline bool save_comment_edit(ReviewComponent& review) {
    if (review.composer != Composer::EditingExisting) return false;
    const auto index = review.editing_index();
    if (index < 0 || static_cast<size_t>(index) >= review.comments.size() ||
        review.composerDraft.text.empty()) return false;
    commit_pending_comment(review);
    return true;
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

// Branch/stash dialog payloads (M24): open state is
// LayoutComponent::modal (NewBranch/DeleteBranch/ForceDeleteBranch/Stash);
// this per-tab struct holds only the dialogs' data.
struct BranchDialogState : public afterhours::BaseComponent {
    std::string newBranchName;
    std::string deleteBranchName;
    std::string stashMessage;
    bool stashIncludeUntracked = false;
};

struct DiffMatch {
    std::string file;
    int line = 0;
    char sign = ' ';
    size_t column = 0;
    int logicalColumn = 1;
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
                    matches.push_back({file.filePath, number, sign, at - 1, reading::column_at_byte(std::string_view(line).substr(1), at - 1)});
                if (sign != '+') ++oldLine;
                if (sign != '-') ++newLine;
            }
        }
    }
    return matches;
}

struct FilePickerScope {
    std::optional<reading::RequestStamp> owner;
    reading::SourceRevision documentRevision;
    bool workingTree = false;
    reading::RequestStamp request;
    git::PathList listing;
    async_work::Task<git::PathList> future;
    git::catalog::Kind category = git::catalog::Kind::All;
    bool catalogStarted = false;
    git::catalog::Page localRefs, remoteRefs, commits;
    git::catalog::Changes changes;
    async_work::Task<git::catalog::Changes> changesFuture;
    async_work::Task<git::catalog::Page> localFuture, remoteFuture, commitsFuture;
    std::string catalogQuery;
    std::optional<std::chrono::steady_clock::time_point> catalogDue;
    std::uint64_t catalogVersion = 0;
    std::vector<git::catalog::Entry> results;
    git::catalog::Page extra;
    async_work::Task<git::catalog::Page> extraFuture;
    std::string extraKey;
    size_t extraOffset = 0;
    bool moreRequested = false;
    std::string statusPath;
    async_work::Task<std::string> statusFuture;
    std::map<std::string, std::string> worktreeStatus;
    // Async picker matching: results are computed on a worker, previous
    // results stay on screen until the new set lands. The launched match
    // is identified by its RequestStamp (key = the match key); a landed
    // match applies only while the stamp still accepts.
    async_work::Task<std::vector<git::catalog::Entry>> matchFuture;
    reading::RequestStamp matchStamp{};
    std::string matchPendingKey;
    bool matchApplied = false;
    std::optional<std::chrono::steady_clock::time_point> matchDue;
    bool sourcesCached = false;
};

// Picker lifecycle (fsm_todo.md M8+M9): one phase + one mode replace the
// former filePickerOpen/filePickerPreviewing flags on LayoutComponent and
// the lineMode/commandMode/symbolMode/projectSymbolMode bools on
// FilePickerPosition (16 combinations, 5 legal). Closed -> Browsing on
// open(mode); a launched match is Matching (previous results stay), a
// live preview is Previewing, Enter-before-match is Confirming; confirm()
// closes without restore, cancel() closes keeping the restore point for
// the picker system to apply. Picker owns the restore point: open()
// saves it on every open path, so no opener saves its own copy.
enum class PickerPhase { Closed, Browsing, Matching, Previewing, Confirming };

inline std::string_view picker_phase_name(PickerPhase phase) {
    switch (phase) {
        case PickerPhase::Closed: return "Closed";
        case PickerPhase::Browsing: return "Browsing";
        case PickerPhase::Matching: return "Matching";
        case PickerPhase::Previewing: return "Previewing";
        case PickerPhase::Confirming: return "Confirming";
    }
    return "Closed";
}

enum class PickerMode { Files, Line, Symbol, ProjectSymbol, Commands, Repositories };

inline std::string_view picker_mode_name(PickerMode mode) {
    switch (mode) {
        case PickerMode::Files: return "Files";
        case PickerMode::Line: return "Line";
        case PickerMode::Symbol: return "Symbol";
        case PickerMode::ProjectSymbol: return "ProjectSymbol";
        case PickerMode::Commands: return "Commands";
        case PickerMode::Repositories: return "Repositories";
    }
    return "Files";
}

struct Picker {
    PickerPhase phase = PickerPhase::Closed;
    PickerMode mode = PickerMode::Files;
    // Document/anchor saved by open(), restored verbatim by the picker
    // system when the picker closes after a live preview without a
    // confirmed open. confirm() discards it; cancel() keeps it (and
    // previewKey, the live-preview marker) for that restore.
    std::optional<reading::Location> restoreLocation;
    std::optional<reading::ReadingAnchor> restoreAnchor;
    std::string previewKey;
    bool confirmKeep = true;  // Keep vs Preview for a Confirming confirm

    bool is_open() const { return phase != PickerPhase::Closed; }
    bool previewing() const { return phase == PickerPhase::Previewing; }

    void open(PickerMode nextMode, std::optional<reading::Location> location,
              std::optional<reading::ReadingAnchor> anchor) {
        mode = nextMode;
        phase = PickerPhase::Browsing;
        restoreLocation = std::move(location);
        restoreAnchor = std::move(anchor);
        previewKey.clear();
        confirmKeep = true;
    }
    void save_restore(std::optional<reading::Location> location,
                      std::optional<reading::ReadingAnchor> anchor) {
        restoreLocation = std::move(location);
        restoreAnchor = std::move(anchor);
    }
    void begin_matching() {
        if (phase == PickerPhase::Browsing || phase == PickerPhase::Previewing)
            phase = PickerPhase::Matching;
    }
    void finish_matching() {
        if (phase == PickerPhase::Matching)
            phase = previewKey.empty() ? PickerPhase::Browsing : PickerPhase::Previewing;
    }
    void begin_preview(std::string key) {
        previewKey = std::move(key);
        if (phase == PickerPhase::Browsing || phase == PickerPhase::Matching)
            phase = PickerPhase::Previewing;
    }
    // A preview is superseded by a chosen entry that has not (yet) closed
    // the picker: keep the key so the preview trigger does not re-preview
    // it in the same frame, but the phase is no longer Previewing.
    void end_preview(std::string key) {
        previewKey = std::move(key);
        if (phase == PickerPhase::Previewing) phase = PickerPhase::Browsing;
    }
    void clear_preview() {
        previewKey.clear();
        if (phase == PickerPhase::Previewing) phase = PickerPhase::Browsing;
    }
    void request_confirm(bool keep) {
        confirmKeep = keep;
        phase = PickerPhase::Confirming;
    }
    // The Confirming request has been consumed by a landed match.
    void consume_confirm() {
        if (phase == PickerPhase::Confirming)
            phase = previewKey.empty() ? PickerPhase::Browsing : PickerPhase::Previewing;
    }
    void confirm() {
        phase = PickerPhase::Closed;
        previewKey.clear();
        restoreLocation.reset();
        restoreAnchor.reset();
    }
    void cancel() { phase = PickerPhase::Closed; }
    void reset() {
        phase = PickerPhase::Closed;
        previewKey.clear();
        restoreLocation.reset();
        restoreAnchor.reset();
        confirmKeep = true;
    }
};

// Repo picker state (fsm_todo.md M28): the global Cmd+Alt+O picker and
// the welcome-screen repository list are one picker with two surfaces, so
// they share this single field set instead of the former
// repositoryPicker* (welcome) + repoPicker* (modal) duplicates. Open/close
// lives in the shared Picker shell (PickerMode::Repositories on
// LayoutComponent::picker); this scope holds only the repo-specific data:
// the shared filter/selection, the welcome sort toggle, and the async
// path resolution (existence + worktree grouping) that fills the entries.
// Entry building itself stays in util/repo_picker.h (build/rank).
struct RepoPickerScope {
    std::string query;
    int index = 0;
    bool focusPending = true;  // focus the filter input on next render
    bool alphabetical = false;  // welcome surface: sort by name vs recent
    async_work::Task<std::vector<repo_picker::Resolved>> future;
    std::vector<repo_picker::Resolved> resolved;
    std::string resolveKey;

    bool resolving() const { return future.valid(); }
    // Shared selection handling for both surfaces: clamp into range, then
    // one step per key press (the caller still owns Enter, whose action is
    // surface-specific).
    void clamp_index(int count) {
        index = count <= 0 ? 0 : std::clamp(index, 0, count - 1);
    }
    void apply_keys(int count, bool down, bool up) {
        clamp_index(count);
        if (count <= 0) return;
        if (down) index = std::min(index + 1, count - 1);
        if (up) index = std::max(0, index - 1);
    }
};

struct FilePickerPosition {
    // Command palette: the menu commands captured when it opened. The
    // picker's mode is Picker::mode on LayoutComponent (M9), not a bool
    // per mode here.
    struct Command {
        std::string label, shortcut;
        std::function<void()> action;
        int line = 0;  // symbol pickers: target line for live preview
        std::string path;  // project symbols: target file
    };
    std::vector<Command> commands;
    std::vector<std::string> queryHistory;
    int queryRecall = -1;
    // Live-preview state for Go to Line / Go to Symbol.
    int previewLine = 0, previewColumn = 0, previewIndex = -1;
    // Go to Symbol reuses the palette; its commands arrive with the scan.
    async_work::Task<OutlineResult> symbols;
    async_work::Task<ProjectSymbolsResult> projectSymbols;
    std::string projectQuery;
    std::optional<std::chrono::steady_clock::time_point> projectDue;
    std::string input;
    std::optional<reading::ReadingAnchor> point;
    file_query::Query parsed;
    reading::RequestStamp request;
    async_work::Task<git::SourcePosition> future;
    std::string key;
    std::string error;
    reading::OpenMode mode = reading::OpenMode::Keep;
};

// Relink dialog payload (M24): open state is modal == RelinkDialog.
struct RelinkDialogPayload {
    std::string oldPath, newPath, error, request;
    async_work::Task<git::GitResult> future;
};

struct LayoutComponent : public afterhours::BaseComponent {
    // Stored modal state (M24): the one open dialog, if any. Writers open
    // via open_modal() and close via close_modal()/close_top_modal();
    // rendering reads modal/modal_is(). Per-dialog data lives in the
    // payload structs (pushDialog, BranchDialogState, relinkDialog,
    // comparison fields, foldPatternsText). Stashed per tab owner like
    // focus (see focus_system.h) so dialogs survive tab switches.
    std::optional<reading::focus::Popup> modal;
    std::map<int, std::optional<reading::focus::Popup>> modalByOwner;
    bool modal_is(reading::focus::Popup popup) const { return modal == popup; }
    void open_modal(reading::focus::Popup popup) { modal = popup; }
    void close_modal(reading::focus::Popup popup) {
        if (modal == popup) modal.reset();
    }
    void close_top_modal() { modal.reset(); }
    std::string_view modal_name() const {
        return modal ? reading::focus::popup_name(*modal) : "None";
    }
    reading::focus::State focus;
    // Focus state stashed per repo tab so switching back restores it
    // instead of resetting (keyed by tab entity id, capped).
    std::map<int, reading::focus::State> focusByOwner;
    int focusRepositoryOwner = -1;
    FilePickerScope filePickerScope;
    FilePickerPosition filePickerPosition;
    // Repo picker (M28): one scope for the welcome list and the global
    // Cmd+Alt+O modal; the modal's open/close is the shared Picker shell
    // in Repositories mode (see repo_picker_open()/open_repo_picker_shell()
    // below), not a bool of its own.
    RepoPickerScope repoPicker;
    RelinkDialogPayload relinkDialog;
    // Picker machine (M8+M9): phase + mode + restore point in one place.
    // open_picker() is the one open path: it saves the restore point from
    // the active repo on every open; confirm_picker()/cancel_picker()
    // close it (confirm discards the restore point, cancel keeps it for
    // the picker system's exact restore).
    Picker picker;
    bool filePickerFocus = false;
    void open_picker(PickerMode mode, const RepoComponent& repo) {
        std::optional<reading::ReadingAnchor> anchor;
        if (const auto* document = repo.workspace().document(repo.workspace().active_id()))
            anchor = document->anchor;
        picker.open(mode, repo.workspace().location(), anchor);
        filePickerFocus = true;
    }
    void open_picker(PickerMode mode) {
        picker.open(mode, std::nullopt, std::nullopt);
        filePickerFocus = true;
    }
    void confirm_picker() { picker.confirm(); }
    void cancel_picker() { picker.cancel(); }
    // Repo picker on the same shell (M28): open is Browsing in
    // Repositories mode (no document restore point — confirming switches
    // repository, cancelling just closes), with the shared filter focused
    // and the selection reset. The resolve future is launched by
    // open_repo_picker() in repo_picker.h, which owns entry building.
    bool repo_picker_open() const {
        return picker.is_open() && picker.mode == PickerMode::Repositories;
    }
    void open_repo_picker_shell() {
        picker.open(PickerMode::Repositories, std::nullopt, std::nullopt);
        repoPicker.focusPending = true;
        repoPicker.index = 0;
    }
    std::string filePickerQuery;
    std::string filePickerCacheKey;
    std::string filePickerSelectionKey;
    std::string filePickerSelectedPath;
    int filePickerIndex = 0;
    bool visibleWhitespace = false;
    bool blameGutter = false;
    bool minimap = false;
    bool stickyScopes = true;
    bool outlinePanelOpen = false;
    int outlinePanelClickLine = 0;
    std::chrono::steady_clock::time_point outlinePanelClickAt{};
    bool showLineNumbers = true;
    static constexpr float kDefaultSidebarWidth = 280.0f;
    static constexpr float kCommitSplitterHeight = 16.f;
    float sidebarWidth = kDefaultSidebarWidth;
    float sidebarMinWidth = 200.0f;
    float commitLogRatio = 0.4f;
    // Minimum height the sidebar system asks layout to reserve for the
    // upper (files) pane — repo header + fixed chrome + a usable list —
    // so a short window takes space from the history pane instead of
    // squeezing the file list to a row or two. 0 = no floor requested.
    float sidebarFilesFloor = 0.0f;

    enum class SidebarMode { Changes, Refs };
    SidebarMode sidebarMode = SidebarMode::Changes;
    enum class SidebarNavigation { Review, Files };
    // Legacy state: the Review/Files toggle is gone (one unified sidebar),
    // kept only so stored layouts and the reveal-in-tree menu still parse.
    SidebarNavigation sidebarNavigation = SidebarNavigation::Files;
    // Review tabs within the Changes view (mock: To review / Approved / Untracked).
    enum class ReviewTab { ToReview, Staged, Untracked };
    ReviewTab reviewTab = ReviewTab::ToReview;

    enum class FileViewMode { Flat, Tree, All };
    FileViewMode fileViewMode = FileViewMode::Flat;
    std::map<std::string, std::set<std::string>> collapsedDirectories;

    enum class DiffViewMode { Inline, SideBySide };
    DiffViewMode diffViewMode = DiffViewMode::Inline;
    std::string foldPatternsText;
    std::string foldPatternsRepo;

    bool sidebarVisible = true;
    bool commandLogVisible = false;
    // Shelf: when collapsed, the diff pane is hidden and the sidebar fills the
    // window (Bear-like). Derived each frame from whether anything is selected;
    // set by LayoutUpdateSystem and read by the sidebar/main-content renderers.
    bool shelfCollapsed = false;
    std::optional<bool> readingPanelCollapsed;
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

// Commit-editor lifecycle (fsm_todo.md M16): Editing (composing) ->
// Committing on request_commit() (the request is pending until the
// sidebar handler either launches commitFuture, still Committing, or
// opens the unstaged-policy dialog, AwaitingPolicy) -> Done on a
// successful poll, back to Editing on failure or dismissal. Done ->
// request_commit() starts the next commit. The phases are the only
// commit-request/dialog state: the former one-frame commitRequested
// flag and showUnstagedDialog bool are deleted, not wrapped.
enum class CommitFlow { Editing, AwaitingPolicy, Committing, Done };

inline std::string_view commit_flow_name(CommitFlow flow) {
    switch (flow) {
        case CommitFlow::Editing: return "Editing";
        case CommitFlow::AwaitingPolicy: return "AwaitingPolicy";
        case CommitFlow::Committing: return "Committing";
        case CommitFlow::Done: return "Done";
    }
    return "Editing";
}

struct CommitEditorComponent : public afterhours::BaseComponent {
    std::string subject;
    std::string body;
    bool isVisible = false;
    bool isAmend = false;
    // The repository's commit.template, comment lines stripped; refills the
    // box after each commit. Read once per tab.
    std::string templateText;
    bool templateLoaded = false;
    // `git config commit.template` in flight; it used to run synchronously on
    // the UI thread the first time the sidebar drew (hundreds of ms per spawn).
    async_work::Task<git::GitResult> templateTask;
    // Where to put the caret after the box's text was replaced from outside
    // (template refill, prefix), which otherwise lands at the very end.
    std::optional<size_t> placeCaret;

    enum class UnstagedPolicy { Ask, StageAll, CommitStagedOnly };
    UnstagedPolicy unstagedPolicy = UnstagedPolicy::Ask;
    // One-shot policy from the commit split button; consumed by the
    // commit handler so the main button keeps its Ask behaviour.
    std::optional<UnstagedPolicy> commitPolicyOverride;
    // Async commit: staging (optional) then commit run in one worker
    // future; commit_workflow::poll_commit applies the outcome.
    struct CommitOutcome { bool stageFailed = false; git::GitResult result; };
    async_work::Task<CommitOutcome> commitFuture;
    std::string commitMessage;

    // Commit workflow state (T030, fsm_todo.md M16). All transitions
    // go through the methods below; rendering reads `flow` (the dialog
    // is open exactly in AwaitingPolicy, a commit is in flight exactly
    // when Committing with a valid commitFuture).
    CommitFlow flow = CommitFlow::Editing;
    bool rememberChoice = false;  // "Remember this choice" checkbox state

    // The one transition every commit entry point (Enter, Commit
    // button, split button, menu, toolbar) calls instead of setting a
    // flag. Idempotent while a request is pending, the policy dialog
    // is open, or a commit is in flight. Never touches
    // commitPolicyOverride: the split button sets its one-shot
    // override before calling this, and the sidebar handler consumes
    // it exactly as the flag handler did.
    void request_commit() {
        if (flow == CommitFlow::Editing || flow == CommitFlow::Done)
            flow = CommitFlow::Committing;
    }
    // A request the sidebar handler has not yet turned into a launched
    // commitFuture (it may still open the policy dialog instead).
    bool commit_requested() const {
        return flow == CommitFlow::Committing && !commitFuture.valid();
    }
    void await_policy() {
        flow = CommitFlow::AwaitingPolicy;
        rememberChoice = false;
    }
    void dismiss_policy() {
        if (flow == CommitFlow::AwaitingPolicy) flow = CommitFlow::Editing;
    }
    // Review-workspace suppression / reset: drops a pending request or
    // an open dialog, never an in-flight commit (its future is polled
    // when the working view returns).
    void cancel_request() {
        if (flow == CommitFlow::AwaitingPolicy) flow = CommitFlow::Editing;
        else if (commit_requested()) flow = CommitFlow::Editing;
    }
    void begin_committing() { flow = CommitFlow::Committing; }
    void abort_commit() { flow = CommitFlow::Editing; }
    void finish_commit() { flow = CommitFlow::Done; }
};

struct MenuComponent : public afterhours::BaseComponent {
    int activeMenuIndex = -1;

    enum class PendingDialog { None, OpenRepo };
    PendingDialog pendingDialog = PendingDialog::None;
    bool paletteRequested = false;

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

    // Display name cache: resolving the folder name hits the filesystem, so
    // TabSyncSystem recomputes it only when the repository path changes.
    std::string labelPath;
    std::string labelBase;
    // When this tab last lost focus; switching back after a while requests a
    // refresh because only the active tab is watched for file changes.
    std::chrono::steady_clock::time_point deactivatedAt{};
};

struct TabStripComponent : public afterhours::BaseComponent {
    std::vector<afterhours::EntityID> tabOrder;
    // Most-recently-used tab order (front = current), for the repo switcher.
    std::vector<afterhours::EntityID> mruOrder;
};

// Tracks in-flight network git operations (push/pull/fetch) so the UI
// thread is never blocked. Each op is an async_slot (shared Phase, see
// fsm_todo.md M21): Pending from enqueue until the polling system
// consumes it as Ready/Failed. `startedAt` drives the status-bar elapsed
// display so a hung push is distinguishable from a slow one. Staleness
// is not a RequestStamp: the polling system refreshes only if the tab's
// repository still matches `repository` (the slot is unstamped).
struct PendingNetworkOp {
    std::string label;
    async_slot::Slot<git::GitResult> slot;
    afterhours::EntityID tabId{0};
    std::string repository;
    std::chrono::steady_clock::time_point startedAt{};

    bool busy() const { return slot.busy(); }
    async_slot::Phase phase() const { return slot.phase; }
    std::chrono::steady_clock::duration elapsed(
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const {
        return startedAt.time_since_epoch().count() == 0
            ? std::chrono::steady_clock::duration::zero() : now - startedAt;
    }
    // Request stop via Task::cancel(); the op stays Pending and the
    // polling system consumes the cancelled result (toast + refresh),
    // exactly like the status-bar Cancel for reads.
    void cancel() { if (slot.task.valid()) slot.task.cancel(); }
};

struct NetworkOpsComponent : public afterhours::BaseComponent {
    std::vector<PendingNetworkOp> pending;

    bool busy() const {
        return std::any_of(pending.begin(), pending.end(),
                           [](const PendingNetworkOp& op) { return op.busy(); });
    }
    // Aggregate phase for the e2e `validate async:network_ops` key: any
    // in-flight op makes the machine Pending, otherwise Idle.
    async_slot::Phase phase() const {
        return busy() ? async_slot::Phase::Pending : async_slot::Phase::Idle;
    }
};

} // namespace ecs

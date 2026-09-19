# Commit Plus triage implementation

September 19, 2026. Implements the 32 **Do** decisions in `triage.md` against
`1a2fcf3`. Skip and Defer decisions remain unchanged. No Commit Plus code was
copied. The separate Afterhours overscan patch remains unmerged.

## Delivered behavior

| Decisions | Implementation |
| --- | --- |
| CP-001, CP-002, CP-003 | Duplicate immutable patch requests share an in-flight read. Cancelling one caller preserves another. Local and remote ref lists publish independently. Repository status distinguishes loading/failure from clean. |
| CP-006 | Prepared an upstream-only Afterhours overscan patch with configurable bounded buffers, precomputed variable-height metrics, and large-jump coverage. No vendor change or cache-budget increase. |
| CP-007, CP-009 | Split-diff intraline comparisons run after visibility culling. Existing syntax token work remains behind culling. Active reads expose Cancel and retain a Cancelling state until the worker finishes; non-cancellable writes do not offer Cancel. |
| CP-011–CP-014 | Quick Open combines typed files, commits, branches, and tags, retains results while changing category, resolves pasted hashes, and labels revision-specific file changes. |
| CP-015, CP-016 | History offers current branch, chosen branch, all refs, and optional remotes, with per-scope selection/position. Commit details open in a bounded, scrollable popover without moving the reader. |
| CP-017–CP-020 | Shift ranges and Cmd toggles use stable commit hashes. Context menus copy selected hashes/subjects in visible order or review a contiguous selection as an endpoint comparison. Tags resolve to immutable objects before comparison. |
| CP-021, CP-022 | Cmd-click changed lines to collect separate feedback ranges. Drafts, saved comments, and Markdown retain those ranges independently of plain text selection. Stash reviews explicitly choose staged, working, or untracked content without applying the stash. |
| CP-037, CP-041, CP-042, CP-079 | Repository navigation exposes paginated reflog/stashes, linked worktrees, and submodules. Worktree status uses bounded worker reads; opening one retains the original repository workspace. Submodules distinguish the gitlink from the actual checkout and report missing/uninitialized state. |
| CP-076 | Push previews the chosen remote, destination branch, fetch URL, and push URL. Execution rechecks the current branch and destination identity and uses an explicit refspec. |
| CP-083–CP-085, CP-088 | Repository pins are independent of recency. The virtualized picker filters and sorts. Relinking verifies a remembered commit and retains settings/reviews; original review files remain intact. Changes/history collapse independently and persist per repository. |
| CP-092, CP-095, CP-096, CP-098, CP-099 | Native and in-app menu invocations retain their repository owner. Command logging redacts credentials before capture and bounds output. Git diagnostics identify the executable/version and available commands. Versioned settings and reviews use atomic writes, preserve data on load failure, and migrate legacy review filenames to SHA-256 keys. Offline fixtures cover the new paths. |

## Using the additions

- `Cmd+P`: choose All, Files, Commits, Branches, or Tags. More opens additional
  catalogues; the repository's `…` menu provides the same repository navigation.
- Right-click a tag to compare it with the current historical review.
- Shift/Cmd-select history rows, then right-click to copy or review the range.
- Cmd-click changed code lines, then choose **Comment N lines**. This leaves plain
  text selection independent and never stages the selected code.
- Open a new repository tab to filter/sort pinned and recent repositories. Its row
  context menu includes pinning and relinking.
- **Details** opens commit metadata. Escape closes it and returns to reading.

## Verification and limits

The full unit run passed 48/49 suites; the remaining refresh-lifetime suite exposed
a settings link dependency. Moving identity persistence to tab synchronization
fixed it, and its rerun passed. Updated catalogue, storage, settings, and focus
suites also passed. The real Git shared-read test observes one patch/metadata pair
while two subscribers share it and one cancels.

The visual journeys cover 100%, 140%, and 200% zoom. They inspect actual destinations,
Git state, saved feedback, rendered geometry, and screenshots. Root-range review
asserts the complete 30-line addition, not just that a comparison panel opened.
The repository journey verifies distinct fetch/push URLs, cancels the preview,
relinks a moved repository, and checks retained feedback and the original files.
A 950×850 window at 200% also keeps repository-picker controls within the reader.
Relinking requires a previously remembered commit identity; unavailable identities
produce an error and retain the old records rather than guessing a replacement.

One 200% feedback run exceeded the 20 ms rendering gate: p99 **24.69 ms**. Its
isolated rerun passed at **4.83 ms** (120 frames). Both results are retained; this
is evidence of variability, not a universal frame-time guarantee. The existing
100 ms warm-switch and 50 ms selection targets have not been re-established by
this work. No competitive performance claim against Commit Plus or px0 is made.

The Afterhours experiment passes 270,020 assertions and the existing virtual-list
UI tests. In a deliberately distant-target fixture, the previous range formula
omitted part of the visible window in 9,994–9,998 of 10,000 jumps; the new formula
omits none. Optimized range-query p95 is about 0.46–0.63 μs, with at most 65–72
constructed rows in the recorded overscan variants. These are geometry/query
measurements, not native presentation or GPU blank-frame measurements. The patch
is prepared in `/Users/gabeochoa/p/afterhours-reading-overscan`, remains uncommitted,
and is exported below; app adoption and native presentation benchmarking remain
separate work.

The 3,000-pair split fixture emits 23–76 paired lines per frame across the tested
positions and zooms. Each emitted pair reaches the new comparison branch once;
the previous branch ran for all 3,000 pairs. These counts are derived from emitted
rows and control flow, not an instrumentation counter. All six frame gates passed in both replays: final p99 2.86–8.52 ms, earlier 3.64–9.31 ms.
The syntax change does not eliminate whole-hunk pairing or wrapped-height preparation. Those
larger profiling/refactor items remain in the earlier P0 backlog.

## Evidence

- [Contact sheet](triage-evidence/contact-sheet.jpg)
- [Upstream overscan patch](triage-evidence/afterhours-overscan.patch)
- [Upstream test/benchmark log](triage-evidence/afterhours-overscan.log)
- [Afterhours constraints and workarounds](afterhours-gaps.md)

The [verification manifest](triage-evidence/verification.json) records final replay
paths, binary hashes, the 49 unit suites, and the 14 regression journeys. The
[frame measurements](triage-evidence/frame-measurements.json) retain per-run p50,
p99, maximum, entity counts, and the failed gate. Compressed logs, selected layout
dumps, and scripts accompany the manifest. Full attempts remain under
`output/triage-*`; passing reruns do not replace the original measurements.

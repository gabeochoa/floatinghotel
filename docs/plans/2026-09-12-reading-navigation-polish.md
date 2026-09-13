# Make reading and navigation feel smooth: 60 ordered commits

Baseline: `4ef6c4e`. Make selecting a commit, reading changes, opening related files, and returning to review predictable. Existing Back/Forward, tabs, search, bookmarks, asynchronous loading, caches, review progress, saved baselines, and diff folding are the starting point. The items below describe improvements, not 60 reproduced defects.

Initial source inspection found one review slot and one replaceable source slot.
Opening a working-tree source can clear the selected review commit. History omits
the selected commit file and line destinations. Reading restoration stores pixels
and retries for three frames. Quick Open lists working-tree paths regardless of
the review revision. Source controls use several rows, with explicit paging for
large files.

## Implementation contract

- Keep repository-owned ECS state and the native renderer. Introduce an authoritative `ReadingWorkspace` with document tabs, active document, history, and per-document reading state.
- Use typed working-tree, index, historical-file, commit-review, and comparison destinations. Resolve and retain historical object IDs. Keep originating review context separate from document identity.
- Centralize Open, Activate, Keep, Close, Back, Forward, and Return-to-review. Callers must not coordinate selection, resets, and focus.
- Store logical anchors with path, revision, side, line, column, and fractional viewport position. Restore after matching content and layout readiness; user scrolling cancels restoration.
- Keep inactive tabs lightweight and one active content runtime. Reuse bounded caches. Do not add the proposed 64 MiB cache without measurement.
- Apply asynchronous results only when repository, document, request key, and generation match.
- Finish and verify each numbered implementation before advancing. Each item is one commit. Attach behavioral tests to every commit and screenshots plus geometry checks to visual changes.
- Run commands with `nice -n 10`, add no source comments, and record Afterhours limitations and workarounds in `docs/afterhours-gaps.md`.
- Do not push or merge without a new request.

## Ordered work

Unchecked items remain outstanding. Evidence and commit IDs belong in the completion record below only after their checks pass.

### 01–10: document tabs

- [x] 01. Add a repeatable commit → diff → source → second source → Back baseline with screenshots, layout dumps, input timing, cache activity, and owned content bytes. Reproduce identical destinations and retain before-change evidence.
- [x] 02. Route tree, tabs, search, bookmarks, comments, menus, and test handlers through typed destinations. Remove competing mutable selection authority and frame-sampled history. Verify existing navigation scenarios.
- [x] 03. Replace two content slots with document tabs. Keep two reviews and three files independently selectable; retain only active rendering payloads.
- [x] 04. Single-click replaces one preview; double-click or Enter keeps it. Reuse open destinations and keep the originating review when opening its source. Never replace a kept tab.
- [x] 05. Disambiguate filenames with the shortest parent path and compact revision badges. Review titles show subjects, with short hashes in tooltips. Check duplicate filenames and revisions.
- [x] 06. Scroll the tab strip and provide an open-tabs menu. Reveal the active tab without scrolling the main window. Check narrow windows, long names, many tabs, and zoom.
- [x] 07. Add close buttons, Cmd+W, and Cmd+Shift+T. Retain 20 closed tabs with reading state. Choose the nearest neighbor when closing; open working changes after the final close.
- [x] 08. Drag tabs to reorder with insertion marker, pointer capture, cancellation, and edge scrolling. Never activate close controls or window resizing during a drag.
- [x] 09. Ctrl+Tab and Ctrl+Shift+Tab traverse MRU documents. Show the switcher only while the modifier is held; exclude closed tabs and repository tabs.
- [x] 10. Persist kept destinations, order, active tab, and anchors through settings. Restore lazily, exclude previews, and show missing revisions as unavailable without substituting working-tree content.

### 11–20: navigation and focus

- [x] 11. Retain 256 precise visits including diff file, line, column, side, and origin. Scrolling does not append history. Check same-commit jumps, Forward truncation, and closed-tab visits.
- [x] 12. Open source at the selected or first visible changed line. Deleted lines use the before revision and old path. Return restores the exact review and position.
- [x] 13. Restore logical anchors through zoom, resizing, folding, and inline/split changes after layout readiness. Explicit scrolling cancels pending restoration.
- [x] 14. Restore semantic focus to picker, menu, search-preview, or feedback callers. Never restore into another repository or closed document.
- [x] 15. Route shortcuts by focus region. Text inputs own editing, trees own navigation, code owns reading. Option+Arrow text movement cannot trigger history; hidden review controls cannot activate.
- [x] 16. Escape dismisses the topmost menu, picker, Find, or preview without clearing the document. Cmd+W closes documents; review collapse gets an explicit command. Repeated Escape retains the review.
- [x] 17. Distinguish full-row hover, selected background, keyboard-focus outline, and review indicator without moving contents.
- [x] 18. Tree Up/Down traverses visible rows; Left collapses or selects parent; Right expands or enters; Enter keeps a file. Skip collapsed descendants and reveal focus.
- [x] 19. Tree type-to-select uses a 700 ms prefix timeout. Repeated letters cycle matches. Inputs retain typing; no accidental repository search.
- [x] 20. Reveal explicitly navigated files by expanding ancestors and scrolling once. Ordinary code scrolling does not recenter. Historical trees stay distinct from Files.

### 21–30: sidebar and Quick Open

- [x] 21. File selection preserves hunk folds and reveals the header. Only explicit line destinations unfold their hunk. Check source excursions and Back.
- [x] 22. Review-row context menus offer applicable Open diff, Open source, Keep open, Copy relative path, and Reveal actions bound to the clicked revision, not prior selection.
- [x] 23. Compact single-child directory chains. Preserve full path identities/tooltips and correct expansion, filtering, traversal, and duplicate basenames.
- [x] 24. Anchor tree viewport and focus by path through refresh/filtering. Choose the nearest survivor if removed. Insertions above do not shift click targets.
- [x] 25. History Up/Down previews commits with focus retained; Enter keeps. Respect text inputs, avoid staging shortcuts, and let the final rapid selection win.
- [x] 26. Append older history without moving rows or selection. Use an inline loading/error tail; check retry and repository beginning.
- [x] 27. Quick Open is an overlay retaining the review. Dismissal restores exact focus/position without mutating the retained tab.
- [x] 28. Quick Open defaults to the active revision, with explicit working-tree scope. Load historical paths asynchronously and open files deleted from today's checkout.
- [x] 29. Empty Quick Open shows recent files in repository/scope. Rank filename matches before directory matches; highlight characters and disambiguate paths deterministically.
- [x] 30. Support `path:line[:column]` and Ctrl+G. Prefer exact filenames before suffix parsing. Check invalid positions, EOF bounds, and historical destinations.

### 31–40: search and presentation

- [x] 31. Retain repository search in a dismissible pane alongside reading, including query, results, scope, selection, and scroll. Returning needs no rerun.
- [x] 32. Debounce search 150 ms, cancel superseded reads, retain limits, and execute Enter immediately. Older queries cannot publish late.
- [x] 33. Group search results with collapsible file headers, counts, paths, and highlighted spans. Virtualize the flattened list; thousands of results must not render thousands of entities.
- [x] 34. Arrows preview search matches; Enter keeps; Escape focuses search. Preserve result pane and exact revision/line, including deleted files.
- [x] 35. Find becomes a per-document overlay with retained query/match, single-line selection seeding, and code focus on dismissal. Preserve viewport size and anchor.
- [x] 36. Find scans the complete source with the bounded reader on a worker and loads matching pages. Cap matches at 5,000 with a notice; check cancellation, encoding, and later pages.
- [x] 37. Consolidate source navigation/path/revision/overflow into one 32-logical-pixel header. Show more code without shrinking configured fonts.
- [x] 38. Move bookmarks from the permanent row to an on-demand keyboard-accessible navigator. Keep revision and line identity and header actions.
- [x] 39. Commit metadata defaults to a 20-logical-pixel subject of at most two lines plus one metadata row. Disclose full message/details; retain long-message virtualization and merge parents.
- [x] 40. Tree selection defaults to selected-file review, with All files available and choice saved per repository. Progress and feedback still cover the whole review.

### 41–50: code viewer

- [x] 41. Retain hunk locations; show secondary actions on hover/focus with reserved space and context-menu access. Labels must not move.
- [ ] 42. Unify next/previous-change commands across working changes, commits, and comparisons. Reveal below sticky controls; j/k apply outside inputs and announce endpoints.
- [ ] 43. Expand 20 context lines locally above/below a hunk using correct revisions. Merge overlapping display ranges without changing review identities or other folds/positions.
- [ ] 44. Add a visible source caret, subtle active line, and gutter emphasis distinct from hover, selection, and Find at every zoom and diff background.
- [ ] 45. Double-click selects words, triple-click lines, Shift-click extends. Use decoded boundaries/rendered geometry; check tabs, Unicode, both sides, and horizontal scroll.
- [ ] 46. Add read-only arrows, Shift extension, word movement, and line/document boundaries while code owns focus. Scroll only enough to reveal the caret.
- [ ] 47. Cmd+C copies plain selected code; Cmd+Shift+C and Copy with location retain review context. Preserve whitespace, line endings, and revision labels.
- [ ] 48. Store selection source positions beyond rendered rows; resolve through bounded reads and edge-autoscroll dragging. Refuse copies over 8 MiB explicitly.
- [ ] 49. Scroll continuously through at most three adjacent source pages with 256 KiB/4,096-line page limits. Load near edges and compensate anchors on eviction. Check fragments, duplication, and mixed versions.
- [ ] 50. Carry multiline comment/string lexer state across lines/pages for C/C++, Objective-C, Python, JavaScript, and TypeScript. Token keys include incoming state; before/after diff states stay separate.

### 51–60: loading, dock, and follow-up review

- [ ] 51. Fold only proven brace ranges in C/C++, Objective-C, JavaScript, TypeScript, JSON and indentation ranges in Python. Keep line numbers and unfold explicit destinations.
- [ ] 52. Reselecting a tab/tree row does not reload, clear caches, reset selection, or move the viewport. Read counters stay unchanged until source identity changes.
- [ ] 53. Update destination selection/title next frame; delay busy indicators 150 ms. Never label previous content as a new file. Same-document refresh may retain clearly marked old content.
- [ ] 54. After 150 ms stable commit hover/keyboard selection, allow one cancellable immutable patch prefetch using existing cache limits. Reserve foreground capacity and prevent growing queues.
- [ ] 55. Preserve tab, selection, anchor, and expanded width through dock toggles. Resize between Metal frames without tweening/default-width intermediate frames, including manual resizing.
- [ ] 56. Dock is a compact review inbox: changed files above history, outstanding-work summary linked to progress, preview expands reading, hover does not, footer stays fixed.
- [ ] 57. Surface Since last review when a baseline exists, showing identity/time. Use saved contents without silent recapture; replacement remains explicit.
- [ ] 58. Deduplicate changed-since-baseline files and unresolved comments into a follow-up itinerary. Preserve outdated/ambiguous warnings; unchanged resolved work stays excluded.
- [ ] 59. Compose feedback at a selected range, preserve dismissed drafts, and restore reading focus/selection after saving. Comments, resolution, and reviewed status never implicitly stage files.
- [ ] 60. Replay all acceptance journeys, compare baseline, run existing regressions, and record timing/memory, gaps, and evidence. Compilation/static screenshots alone are insufficient.

## Acceptance

Single-click preview, double-click/Enter keep, one preview, independent kept source/review tabs. Preserve changes above history, single-line author-free commit rows, larger configured font, fixed footer, and direct dock resizing. Code stays read-only; no IDE editing, language server, semantic definitions, or broader Git management.

Exercise mouse and keyboard at 100%, 140%, and 200%, narrow expanded windows, and dock mode. Cover duplicate filenames, renames, deletions, root commits, merge parents, missing objects, changed working files, long lines, Unicode, large files, and delayed/out-of-order reads.

Target p95 selection feedback ≤50 ms and cache-resident switches ≤100 ms on the recorded local fixture. Retain 20 ms p99 render gates and existing cache budgets. Separate cold loads from warm navigation; report failures/variability. Startup remains hidden until interactive. Prefer headless checks and use existing hidden native-window probes when actual window behavior matters.

## Completion record

Step 01 passed the optimized build, 23 content/patch unit tests, and nine native headless journeys with 90 navigation samples, PNG/layout checks, and the existing 20 ms p99 render gate. See `docs/reading-navigation-baseline.md` and `output/reading-navigation/baseline-rendered`. Step 02 passed 82 targeted unit checks, seven native navigation regressions, and nine zoom journeys with 90 samples. See `docs/reading-navigation-step02.md`. Step 03 passed 85 unit checks, three native document-tab journeys across all requested zooms, seven navigation regressions, and a cold/warm reading replay. See `docs/reading-navigation-step03.md`. Step 04 passed 54 unit checks, preview and six-kept-tab journeys at three zoom levels, seven navigation regressions, and the priority hover/feedback replay. See `docs/reading-navigation-step04.md`. Step 05 passed 45 focused unit checks, title/label-fit journeys, preview/keep, and compact-close hover at all three zooms. See `docs/reading-navigation-step05.md`. Step 06 passed 18 unit checks, the 25-tab overflow journey and prior tab/hover/title journeys at three zooms, and seven navigation regressions. See `docs/reading-navigation-step06.md`. Step 07 passed 38 navigation unit checks, close/reopen journeys, clean/changed sidebar geometry, and Review/Files divider journeys at three zooms, plus reading-position and dock-resize regressions. See `docs/reading-navigation-step07.md`. Step 08 passed 45 unit checks, the navigation ownership checker, native drag journeys and prior preview/close/overflow/hover regressions at three zooms. See `docs/reading-navigation-step08.md`. Step 09 passed 42 unit checks and modifier-held native switcher, preview, close, and reorder journeys at three zooms. See `docs/reading-navigation-step09.md`. Step 10 passed kept-tab session restoration, lazy loading, unavailable-revision, and source-reader checks. Step 11 passed precise history and closed-document revisit checks. Step 12 passed selected-line, rename, deletion, and exact review-origin return checks. Step 13 passed 58 unit tests, 13 layout comparisons, three-zoom later-page restoration, and the existing navigation, session, close/reopen, wrapping, and window-size regressions. See `docs/reading-navigation-step10.md` through `docs/reading-navigation-step13.md`. Step 14 passed five focus unit tests, three-zoom focus/typing/feedback journeys, and navigation, source-origin, close/reopen, anchor-layout, and recent-tab regressions. See `docs/reading-navigation-step14.md`. Step 15 passed seven focus unit tests, three-zoom shortcut/history/focus journeys, and all navigation regressions. See `docs/reading-navigation-step15.md`. Step 16 passed nine focus unit tests, three-zoom Escape/draft/collapse journeys, six modal geometry checks, logical-anchor regressions, and hidden native resize probes. See `docs/reading-navigation-step16.md`. Step 17 passed 73 review unit tests, three-zoom tree-state geometry and historical-status checks, plus navigation, focus, and source-origin regressions. See `docs/reading-navigation-step17.md`. Step 18 passed 60 unit checks, three-zoom tree keyboard and geometry journeys, the 20 ms render gate, and focus, source, anchor, tree-state, and navigation regressions. See `docs/reading-navigation-step18.md`. Step 19 passed 65 unit checks, three-zoom type-to-select and editing journeys, and keyboard, shortcut, source-origin, and navigation regressions. Git-quoted Unicode paths are decoded; the native runner’s Unicode input limitation is documented. See `docs/reading-navigation-step19.md`. Step 20 passed 68 unit checks, three-zoom reveal and tree-position journeys, and keyboard, typing, focus, source, history, and all seven navigation regressions. Timing variability and failed attempts are retained in `docs/reading-navigation-step20.md`. Step 21 passed three-zoom fold-preservation journeys, 13 logical-anchor comparisons, source-origin and comment-navigation regressions, and 20 review-storage tests. See `docs/reading-navigation-step21.md`. Step 22 passed three-zoom review-tree menu and focus journeys, 14 context-menu tests, and navigation ownership checks. See `docs/reading-navigation-step22.md`. Step 23 passed 43 unit tests, three-zoom compact-tree journeys, and keyboard, reveal, and review-menu regressions. See `docs/reading-navigation-step23.md`. Step 24 passed 20 tree unit tests, three-zoom refresh/filter geometry journeys, and reveal, compact-tree, focus, and keyboard regressions. Failed attempts and timing variability are retained in `docs/reading-navigation-step24.md`. Step 25 passed nine focus unit tests, three-zoom history keyboard, focus, shortcut, and tree journeys, plus seven navigation regressions. See `docs/reading-navigation-step25.md`. Step 26 passed three-zoom pagination, retry, focus, geometry, contrast, and root-commit checks, plus commit-keyboard, refresh-position, focus-return, and seven navigation regressions. See `docs/reading-navigation-step26.md`. Step 27 passed three-zoom picker, container-hover, and focus journeys, all seven navigation regressions, and hidden native resize/GPU checks. Native resize timing variability is recorded in `docs/reading-navigation-step27.md`. Step 28 passed real-Git path-list tests, three-zoom revision/scope, delayed-read, overlay, focus, shortcut, and Escape journeys, and seven navigation regressions. See `docs/reading-navigation-step28.md`. Step 29 passed 87 unit checks, three-zoom ranking/highlight/contrast and scope/overlay/restart/focus journeys, plus seven navigation regressions. See `docs/reading-navigation-step29.md`. Step 30 passed 90 unit tests, three-zoom line/history/focus/shortcut journeys, 13 layout comparisons, and seven navigation regressions. See `docs/reading-navigation-step30.md`. Step 31 passed ten search unit tests, three-zoom retained-pane and comparison-origin journeys, focus/Escape checks, seven navigation regressions, and three search-filter regressions. See `docs/reading-navigation-step31.md`. Step 32 passed three-zoom debounce/cancellation/cap journeys, ten search unit tests, retained search and comparison-origin checks, seven navigation and three filter regressions. See `docs/reading-navigation-step32.md`. Step 33 passed twelve search unit tests, three-zoom grouped/highlight/geometry and retained-search journeys, cancellation, seven navigation and three filter regressions. The missing Japanese glyphs in bundled fonts remain an explicit Afterhours limitation. See `docs/reading-navigation-step33.md`. Step 34 passed thirteen search unit tests, three-zoom keyboard previews through line 5,000 and comparison deletions, focus/Escape, search, and seven navigation regressions. See `docs/reading-navigation-step34.md`. Step 35 passed 48 unit checks, three-zoom per-document Find and unchanged-viewport journeys, focus/shortcut/Escape checks, wrapped Unicode selection seeding, 13 anchor comparisons, and seven navigation regressions. See `docs/reading-navigation-step35.md`. Step 36 passed 70 unit checks, three-zoom full-file/UTF-16/long-line Find, cancellation, pending-Enter, selection, focus, anchor, and seven navigation regressions. The full-page rendering regression and fix are recorded in `docs/reading-navigation-step36.md`. Step 37 passed 19 unit checks, three-zoom header geometry and all moved actions, Find, anchors, focus, tab, and navigation regressions. The menu-key and large-scroll anchor fixes are recorded in `docs/reading-navigation-step37.md`. Step 38 passed three-zoom bookmark identity, saved state, keyboard, missing-object, clipping, and bounded-menu journeys, plus source-header, selection, focus, Escape, navigation, and Markdown checks. See `docs/reading-navigation-step38.md`. Step 39 passed three-zoom compact/expanded metadata, full-message virtualization, per-document state, merge-parent, anchor, and navigation checks, plus 33 unit tests. See `docs/reading-navigation-step39.md`. Step 40 passed selected-file/all-file, complete-review progress and feedback, staged identity, persistence, fold, anchor, comparison, metadata, and navigation journeys at three zooms. See `docs/reading-navigation-step40.md`. Step 41 passed hover/focus reveal, stable hunk geometry, compact icons, context actions, folds, selected review, shortcut ownership, wrapped code, and navigation checks at three zooms. See `docs/reading-navigation-step41.md`. Steps 42–60 remain outstanding.

User priorities added during implementation: provide a commit-style review of all unstaged changes (including new files) with staged changes in a separate view; remove the recurring startup toast; add document-tab close buttons and context menus; reduce commit loading latency; compact the commit header and scroll its metadata away; enlarge default code text by 10% and make Cmd± change text independently of UI zoom; verify hover and pointer targeting at every supported zoom; restore the last window size and remembered expanded width; wrap long source and diff lines within the reading pane; give tab close controls a compact hover background with a larger invisible click target; tighten the graph-dot-to-commit-title gap. The priority implementation and its verification are recorded in `docs/reading-navigation-priorities.md`. Cached switching still misses the 100 ms target. Numbered items remain unchecked until their full acceptance checks pass; work resumes at 42. The duplicate clean/changed status row at the bottom of the sidebar was removed in step 07 at the user’s request.

User follow-up on September 13: focusable containers must not acquire a large
hover fill. Live window resizing currently exposes a white frame; check AppKit's
resize event loop, drawable presentation, and layout timing as well as the final
window size. The focus-region fix and native resize/GPU verification passed in step 27. Full dock-state and physical window journeys remain part of steps 55 and 60.

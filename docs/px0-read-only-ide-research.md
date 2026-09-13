# px0 comparison and performance work

Research date: September 13, 2026. Upstream [px0 commit `5743e84`](https://github.com/px0-ai/px0/commit/5743e84e6bc4dd2e309200206f957ed540df6976), committed September 13 at 18:52:41 UTC, version 0.1.1. Floatinghotel source inspected at `d0d9dd0db0c102c721f96d7b284bfc46ff3d78fe`, with the step 60 reader-width adjustment and completed verification. This report uses current source and the [60-step plan](plans/2026-09-12-reading-navigation-polish.md), not the original two-tab baseline.

Floatinghotel covers the main reading workflow: files, tabs, search, Find, selection, copy, wrapping, history, and large-file paging. The most useful additions from px0 are document symbols, identifier occurrences, and Markdown navigation. Warm source and review switching need performance work before a claim of faster navigation is defensible.

This was source and documentation research only. No px0 build, application, or benchmark ran. Upstream performance numbers below are published claims, not measurements reproduced here. Possible failure modes identified in source need runtime reproduction.

## Feature comparison

“Present” means the reader capability exists, not identical behavior. “Partial” names the difference. “Missing” means no implementation was found in the inspected reading paths. “Out of scope” refers to the original plan's explicit exclusions. Rows describe capabilities, not a weighted parity score.

| Reader capability | px0 evidence | Floatinghotel status and evidence |
| --- | --- | --- |
| File explorer and reveal | [Tree implementation][px-tree] | Present. Keyboard traversal, type-to-select, compact chains, stable reveal. [Steps 18–24](plans/2026-09-12-reading-navigation-polish.md). |
| Fuzzy file picker and recent destinations | [File palette][px-palette] | Present. Revision-aware Quick Open, filename ranking, highlights, recent files. [Steps 27–30](reading-navigation-step29.md). |
| Go to line and column | [Palette][px-palette], [file opening][px-tabs] | Present. `path:line:column` and Ctrl+G, including historical files. [Step 30](reading-navigation-step30.md). |
| Tabs, close, reopen, keyboard switching | [Tabs][px-tabs] | Present. Preview and kept tabs, MRU, reorder, persistence, independent reviews. [Steps 3–10](plans/2026-09-12-reading-navigation-polish.md). |
| Back and Forward | [History][px-history] stores path, line, and column | Present. Logical anchors also retain revision, side, and review origin. [Steps 11–13](reading-navigation-step13.md). |
| Repository literal, regex, case, word, and glob search | [Search API][px-search-api] and [pane][px-search] | Present. Retained revision-aware pane, cancellation, grouped virtualized results. [Search implementation](../src/git/repository_search.cpp), [steps 31–34](reading-navigation-step34.md). Regex dialects still need common fixtures. |
| Find within a file | [Find][px-find] | Present. Per-document state and complete-file worker scan, including later pages. [Steps 35–36](reading-navigation-step36.md). |
| Caret, selection, copy, copy reference | [Caret][px-cursor] and [selection actions][px-selbar] | Present. Read-only keyboard selection, Unicode boundaries, cross-page selection, revision-aware copy. [Steps 44–48](reading-navigation-step48.md). |
| Copy a snippet formatted for an agent | [Selection actions][px-selbar] produce a reference and fenced code | Partial. Copy with location and review-feedback export exist. Dedicated source-snippet formatting is absent. [Menu](../src/ui/menu_setup.h), [step 59](reading-navigation-step59.md). |
| Wrap and line numbers | [Renderer][px-renderer] exposes both toggles | Partial. Wrapping and line numbers exist; no line-number visibility toggle found. [Source renderer](../src/ui/full_file_view.h). |
| Large-file viewport rendering | [Renderer][px-renderer] and [highlight reader][px-highlight] | Present with different tradeoffs. Three adjacent pages, each bounded by 256 KiB and 4,096 lines. [Step 49](reading-navigation-step49.md). |
| Syntax language breadth | [README][px-readme] advertises about 280 Chroma languages | Partial. Smaller [language-family mapping](../src/util/code_lexer.h), with multiline state across pages. [Step 50](reading-navigation-step50.md). |
| Document outline and symbol picker | [Regex outline][px-symbols], [outline panel][px-outline], symbol palette | Missing. Existing lexical state and folding do not provide a symbol list. Good next addition without LSP. |
| Selected identifier occurrences | [Renderer decorations][px-renderer] highlight `S.occ` | Missing as an automatic reading aid. Find already highlights explicit queries. A lexical occurrence mode can reuse selection and Find. |
| Find usages and approximate declarations without LSP | [Fallback navigation][px-lsp] calls whole-word search and declaration ranking | Partial. Manual whole-word search exists. A selection-to-search action is missing; declaration ranking is also absent. Results must be labeled textual matches. |
| Semantic definition, references, type hover, call trails | [LSP navigation][px-lsp], [hover][px-hover], [calls][px-calls] | Out of scope in the original plan and absent. These remain a real px0 advantage if the new scope expands to semantic navigation. |
| Language-server discovery and installation | [README setup][px-readme] | Out of scope. No installation work is needed for outline, occurrences, or Markdown links. |
| Markdown preview | [Markdown behavior and limits][px-markdown] | Partial. [Current preview](../src/util/markdown_preview.h) has headings, paragraphs, bullets, code, and logical source positions. Images are explicitly omitted. |
| Markdown links, heading anchors, tables, images, formatted inline text | [Markdown implementation][px-markdown] | Missing from the simple preview parser. Relative links and heading navigation have the highest reading value. |
| Image inspection | [Image opening][px-tabs] and raw-file endpoint | Present for supported images through [image preview and diff](../src/ui/image_diff.h). Format parity needs fixtures. |
| Searchable command palette | [Command list][px-palette] | Missing. Native menus and keyboard help exist. Reuse existing commands if adding a palette. |
| Themes | [README][px-readme] advertises 14 built-in themes | Partial. Dark is usable; the light toggle is explicitly hidden in [menu setup](../src/ui/menu_setup.h) pending complete support. |
| Shortcut help | [README shortcuts][px-readme] | Present. [Keyboard help](../src/ui/keyboard_shortcuts.h). |
| Revision and review workflow | Inspected px0 routes and tabs are working-file based | Present in floatinghotel beyond this comparison: historical source, split and inline diffs, comments, review progress, saved baselines, follow-up itinerary, and dock. [Plan](plans/2026-09-12-reading-navigation-polish.md). No comparable px0 implementation found. |
| Editing, staging, or broader Git management | px0 describes itself as read-only | Out of scope for this parity work. |

## What the performance claims measure

The [px0 benchmark script][px-benchmark] times `curl` HTTP responses and reports the fastest of five requests by default. Its corpus run disables LSP and does not open a browser. RSS reads the Go server process from Linux `/proc`; it excludes browser processes. The reported “Peak” is an RSS sample at the end of the run, not a continuously observed high-water mark.

The [published results][px-benchdocs] give 0.8–13.5 ms fuzzy requests, 2.3–451.8 ms full scans, 26.7–199.0 ms first large-file requests, and 0.6–6.5 ms repeated requests. Indexing ranges from 1 to 566 ms. The headline “under 1 ms startup” is not a measured launch-to-interactive browser journey. The script reports index duration separately and polls readiness at 300 ms intervals.

Floatinghotel's completed [step 60 replay](reading-navigation-step60.md) has 90 offscreen and 90 hidden-native navigation samples in `output/step60-final/reading` and `output/step60-final/reading_native`. Selection maxima are 10.81 ms and 18.49 ms. Warm review samples span 103–201 ms and 106–181 ms; warm source samples span 328–498 ms and 289–634 ms. The 100 ms warm target is missed. The reading journeys passed their 20 ms frame-p99 gates. These are dispatch-to-matching-CPU-frame measurements and exclude OS presentation; the final report retains failed feature-gate attempts and unchanged repeats. They cannot be compared with px0 HTTP minima.

### Architecture that matters to a comparison

- px0 renders visible browser rows plus 24 overscan rows on each side. Scroll events coalesce through `requestAnimationFrame`. Current [source][px-state] uses 1,000-line chunks; its editor internals document still says 500. Source is authoritative here.
- First open still [reads the whole file and scans its lines][px-highlight], then retains the source and line offsets. The 64 MiB file cap and this initial work qualify the claim that a 400,000-line file costs the same as a ten-line file. Visible DOM size is bounded; total open cost is not constant.
- Highlighting uses 1,000-line windows with 400 context lines and a 512 KiB tokenization window limit. Full-file background refinement runs only for files up to 2 MiB. Larger files can retain approximate multiline highlighting. Time to first readable text and time to correct syntax are separate metrics.
- The Go [LRU cache][px-cache] budgets 512 MiB, counts accumulated HTML, and keeps its last entry even over budget. Each browser tab retains fetched HTML chunks, with no chunk eviction found in the inspected path. The advertised startup RSS does not describe a long reading session.
- An existing px0 tab switches without a server read. The benchmark's repeated GET also retains the cache. Actual [close and reopen][px-tabs] differs: closing calls `/api/close`, which evicts the file and requests Go memory release. Benchmark tab switching and closed-tab reopening separately.
- Server cache identity is path, nanosecond mtime, and size. An existing browser tab does not revalidate on activation. Later chunk responses have no content-version token. A file changed between chunks could combine old and new content; same-size writes with preserved mtime can also reuse a server entry. These are source-based risks, not reproduced failures. Floatinghotel explicitly compares [source identity](../src/git/content_reader.cpp) across pages.
- Simultaneous px0 cold opens can both miss the cache before either inserts. Tokenization also happens outside the chunk lock. Neither path visibly deduplicates all concurrent work. [Search][px-search] has 160 ms debounce, but the inspected response path has no generation guard. The [backend search][px-search-api] accepts no request cancellation context. Rapid query changes belong in correctness and CPU-work tests.
- px0 searches indexed files up to 8 MiB, defaults to 200 matching files and 50 matches per file, and uses one worker per CPU. The published no-match scan is literal despite broader regex wording. Match counts, exclusions, byte coverage, and result limits must agree before comparing search speed.

## Recommended follow-up

The existing quiet reading replay already localizes one warm historical-source
case: `output/step60-final/reading/zoom-100-run-1/run.log` records 111 ms for
`rev-parse`, 125 ms for `cat-file`, and 119 ms for `ls-tree` immediately before the
last blob-cache hit. Each reports zero repository-lock wait. The corresponding
`warm_back.reading.json` records 358.20 ms to ready. The three metadata commands
account for about 355 ms of that sample. This is one observed case, not an
isolated command benchmark or a population estimate.

1. **Reduce warm-switch latency first.** Add stage timings around dispatch, queue wait, Git validation, cache lookup, decoding, publication, layout, and final frame. [Historical source reads](../src/git/content_reader.cpp) run `rev-parse`, `cat-file -s`, and a mode lookup before checking the page cache. Cache hits also copy page or patch values. Measure these costs before changing them. One concrete candidate is a bounded `git ls-tree -l -z <resolved commit> -- <literal path>` lookup for mode, type, object ID, and size. It may replace the three metadata processes for historical files. Verify missing blobs, symlinks, gitlinks, and shallow repositories before relying on it; index and working-file reads need separate treatment. A cached immutable descriptor is another candidate after those costs are measured. [Step 54](reading-navigation-step54.md) explains the existing validation requirement. Keep current cache budgets until evidence supports a change.
2. **Add a document symbol picker without LSP.** Reuse Quick Open presentation and typed line destinations. Run a cancellable source scan keyed by content identity. Start with the supported C-family, Python, JavaScript, TypeScript, and Markdown forms. Label approximate results, suppress declarations inside proven strings and comments, and retain the active revision. Verify comments, multiline declarations, duplicate names, cancellation, and later pages.
3. **Add selected-identifier occurrences and Search this identifier.** Reuse decoded word boundaries and the existing search pane. Preserve the user's Find query and reading selection. Whole-word textual results must not claim semantic references. Test Unicode, comments, shadowed names, historical files, and stale reads.
4. **Make Markdown links useful.** Add heading anchors and relative links through typed revision-aware navigation, including Back and Forward. Then add tables, inline formatting, and local images as separate bounded changes. Preserve source-to-preview position and explicit limits. No browser HTML renderer is required to get the navigation benefit.
5. **Improve discovery and language coverage after those journeys work.** A command palette, formatted snippet copy, and a complete light theme are smaller parity gaps. Add languages based on real repositories, with page-boundary fixtures. Semantic hover, definitions, references, and calls require a separate scope decision because the original plan excludes them.

## Comparable benchmark specification

The next benchmark should produce raw event records and a report for the same user actions on both applications. Proposed setup follows; no competitive result exists yet.

| Dimension | Required setup |
| --- | --- |
| Versions and machine | Pin both app commits, optimized builds, corpus commits, browser version, OS, CPU, RAM, display scale, and refresh rate. Run sequentially at `nice -n 10` on the same machine after step 60 finishes. |
| Scope | Working-tree reading with px0 `-no-lsp` is the shared baseline. Record Git review timings separately. If semantic support is added, use the same server versions and include their resources. |
| Fixtures | Use a small deterministic repository first, then pinned Flask, Redis, React, and TypeScript snapshots. Add a 400,000-line file below 64 MiB, a long-line file, Unicode and CRLF files, and Markdown links. Record indexed paths and searched byte coverage. |
| Display | Match viewport dimensions, physical text size, wrap mode, and visible content. Run 100%, 140%, and 200% plus a narrow window. Use a dedicated browser profile. |
| Cache states | Separate fresh process with warm OS cache, documented cold-storage runs where practical, already-open tab switching, backend-cache reopen, actual close/reopen, and memory-pressure eviction. Never call a process restart a cold-disk run. |
| Memory | Report the px0 server and browser process group separately and together, plus floatinghotel's process group. Include GPU resources where measurable. Sample peaks continuously through 20 tabs, repeated scrolling, closure, and 30 seconds idle. Report owned cache bytes separately from resident memory. |
| Repetition | Use at least 30 launches and 100 measured repetitions per warm journey across several alternating app blocks. Keep every sample, timeout, and failure. Report p50, p95, maximum, and uncertainty. Use at least 1,000 frames for frame p99. |

Shared journeys are tree-open, fuzzy-file search to source, tab A to B to A, Back and Forward, Find to a later page, repository search to a matching line, continuous scroll, selection and copy, and Markdown source/preview. Search cases include no match, one match, dense matches, literal and regex queries, Unicode case behavior, and rapid supersession. Compare actual returned matches and clipboard bytes.

Each event needs an input timestamp, immediate selection-feedback timestamp, correct visible-content timestamp, and content identity. Browser fetch completion and floatinghotel worker completion are diagnostic stages. Neither is the endpoint. Browser tracing must include paint/compositor presentation; native tracing must include drawable presentation. Until those paths are instrumented consistently, report CPU-frame measurements separately and make no input-to-photon claim. A synchronized screen recording can corroborate visible results at its stated frame resolution.

Correctness stress cases include changing a file while reading, changing it between chunks, overlapping queries, rapid A/B/A opens, closing while loading, multiline syntax across a distant page, and cache eviction. Record limitations and failures alongside latency. A stale or blank frame does not count as successful completion.

Proposed acceptance targets:

- Preserve floatinghotel's p95 selection feedback ≤50 ms, cache-resident correct-content switch ≤100 ms, and CPU render p99 ≤20 ms. Add separately measured presentation latency.
- Treat a ≥20% lower p95 on a predeclared shared journey, with uncertainty excluding parity, as evidence of a useful win. Publish all journeys rather than selecting only favorable ones. No required correctness case may regress.
- Keep memory within existing floatinghotel budgets. Compare total application-plus-browser memory for px0 before making footprint claims. Search must perform equivalent work, with equal result limits and documented exclusions.

[px-readme]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/README.md
[px-tree]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/tree.js
[px-palette]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/palette.js#L22-L48
[px-tabs]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/tabs.js#L20-L152
[px-history]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/history.js
[px-search-api]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/search.go#L267-L340
[px-search]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/search.js#L10-L55
[px-find]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/find.js
[px-cursor]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/cursor.js
[px-selbar]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/selbar.js#L118-L132
[px-renderer]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/renderer.js
[px-highlight]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/highlight.go
[px-symbols]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/symbols.go#L159-L215
[px-outline]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/outline.js
[px-lsp]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/lsp.js
[px-hover]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/hover.js
[px-calls]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/calls.js
[px-markdown]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/docs/internals/markdown.md
[px-benchmark]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/benchmark.sh#L77-L170
[px-benchdocs]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/BENCHMARKS.md
[px-state]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/state.js#L56
[px-cache]: https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/highlight.go#L425-L555


## Reproducibility appendix

These commands are prepared for use after floatinghotel's acceptance replay. None of the builds, binary downloads, or launches below ran during this research.

Tool inspection on this arm64 Mac found Node `v26.5.0`, npm `11.17.0`, Git, curl, and Google Chrome `153.0.8010.36`. Go was absent from PATH and the standard Homebrew and `/usr/local/go` locations. The pinned [go.mod](https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/go.mod) requires Go 1.25, despite the README's 1.24 statement. No browser CLI, browser connector tools, or locally resolvable Playwright/Puppeteer packages were found. Node's built-in `WebSocket` is available for Chrome DevTools Protocol, CDP, without installing a package.

The research files are at `/tmp/px0-read-only-research`. This is a partial raw-source cache, not a Git checkout or a buildable source tree. A later source build can use a separate checkout:

```sh
nice -n 10 git clone --no-checkout https://github.com/px0-ai/px0.git /tmp/px0-build-5743e84
nice -n 10 git -C /tmp/px0-build-5743e84 checkout --detach 5743e84e6bc4dd2e309200206f957ed540df6976
nice -n 10 make -C /tmp/px0-build-5743e84 build
```

The build requires an available Go 1.25-or-newer toolchain. The [Makefile](https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/Makefile) runs the dependency-free Node bundler, then `go build -trimpath -ldflags="-s -w" -o px0 .`. Go dependencies may need downloading before timing begins. The minimal build using the already committed web bundle is `nice -n 10 go -C /tmp/px0-build-5743e84 build -trimpath -ldflags="-s -w" -o px0 .`. Record which path was used.

An official prebuilt alternative avoids installing Go. The annotated [v0.1.1 tag](https://api.github.com/repos/px0-ai/px0/git/tags/1e1f0990f5b92975f2375933adab86ad04a35269) resolves to the exact inspected commit. The [release API](https://api.github.com/repos/px0-ai/px0/releases/tags/v0.1.1) publishes the arm64 asset and its SHA-256 digest:

```text
https://github.com/px0-ai/px0/releases/download/v0.1.1/px0-0.1.1-darwin-arm64
7d075e6e15c37d00596f78487acd218942a0f62b0724a2fad32cd8eaddefcfa1
```

The release also publishes `checksums.txt`. A later download and digest check can use:

```sh
nice -n 10 curl --fail --location --output /tmp/px0-0.1.1-darwin-arm64 https://github.com/px0-ai/px0/releases/download/v0.1.1/px0-0.1.1-darwin-arm64
nice -n 10 shasum -a 256 /tmp/px0-0.1.1-darwin-arm64
nice -n 10 chmod u+x /tmp/px0-0.1.1-darwin-arm64
```

Verify the printed digest before execution. Release metadata links the asset to the tag; this research did not independently reproduce the binary from source.

Use a disposable fixture rather than the active repository. This creates a separate Git directory and checks out the floatinghotel source revision used by this report:

```sh
nice -n 10 git clone --no-hardlinks --no-checkout /Users/gabeochoa/p/floatinghotel /tmp/px0-comparison-fixture
nice -n 10 git -C /tmp/px0-comparison-fixture checkout --detach d0d9dd0db0c102c721f96d7b284bfc46ff3d78fe
nice -n 10 /tmp/px0-0.1.1-darwin-arm64 -no-open -no-lsp -quiet -host 127.0.0.1 -port 17777 /tmp/px0-comparison-fixture
```

Run the final command in a dedicated terminal and retain its PID. For a source build, substitute `/tmp/px0-build-5743e84/px0`. Serve the same fixture to floatinghotel. Submodules remain unpopulated unless explicitly prepared in this disposable copy. Record the included file list. `-quiet` also suppresses the daily update request and update-state write in [checkDailyUpdate](https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/update.go#L158-L163). No LSP or installer action is needed.

Confirm `/api/meta` reports the expected root, version, `ready: true`, and empty `lspServers` before timing. px0 may choose a different port if 17777 is occupied, so verify the owning process or use its actual listening port. Start an isolated Chrome instance with a new profile directory:

```sh
nice -n 10 curl --fail http://127.0.0.1:17777/api/meta
nice -n 10 "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome" --user-data-dir=/tmp/px0-chrome-5743e84 --remote-debugging-port=0 --no-first-run --no-default-browser-check --window-size=1280,900 http://127.0.0.1:17777/
```

Use a fresh profile path for a fresh-profile run. Preserve it for explicitly warm sessions. The separate profile prevents reuse of the user's normal Chrome session. `DevToolsActivePort` inside the profile gives the assigned CDP port and browser WebSocket path. Node `fetch` can read `http://127.0.0.1:<cdp-port>/json/list`; Node `WebSocket` can connect to the target's `webSocketDebuggerUrl`. Track this Chrome process group and close it through CDP `Browser.close` after the run.

Existing observation points require no px0 source edits:

- `/api/meta` exposes readiness, indexed file count, index milliseconds, version, root, and server availability. `/api/file` exposes path, start, line content, total, and syntax exactness. Neither endpoint means that pixels are visible.
- DOM selectors `#tabs .tab.active`, `#rows .row[data-l] .c`, `#viewport`, `#q`, `#results`, and `#mdview` expose active path, rendered code, scrolling, search, and preview. The active tab's `title` contains the path. The release bundle encloses application state in an IIFE, so a runner must not assume `window.S` or `window.openFile` exists.
- CDP `Input.dispatchMouseEvent` and `Input.dispatchKeyEvent` drive actual controls. `Runtime.evaluate` can collect DOM identity and content checks. `Network` events capture request stages. `Tracing` with timeline, user-timing, and compositor categories captures render work. A temporary injected event listener can mark input handling; a DOM mutation observer can mark matching content. Those marks precede presentation and must be labeled accordingly.
- On macOS, sample the tracked server, Chrome browser, renderer, utility, and GPU PIDs with `nice -n 10 ps -axo pid=,ppid=,rss=,time=,command=`. RSS is KiB; sample over time for observed peak RSS. CPU `time` is cumulative, so derive utilization from deltas or use a native profiler for short intervals. Report per-process and summed RSS because shared pages can be counted twice. Record sample cadence and instrument both apps equally.

Do not use px0's `/api/metrics` as native macOS RSS or CPU evidence. Its [implementation](https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/metrics.go) falls back to Go `MemStats.Sys` when `/proc` is absent, and CPU sampling returns its previous value. Also retain the unmodified UI's idle behavior: [status polling](https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/web/src/status.js#L84-L94) requests metrics every 2.5 seconds, while [server reclamation](https://github.com/px0-ai/px0/blob/5743e84e6bc4dd2e309200206f957ed540df6976/server.go#L86-L106) requires 15 seconds without requests. The HTTP-only benchmark's idle-memory recovery may therefore differ from an open browser session. This implication needs runtime confirmation.


## Static audit of the selection-extent frame gate

The unchanged-frame sample in `output/step60-tail/selection_extent/100-split_after/journey.log` reports 120 frames, 484 entities, 14.34 ms average, 20.47 ms p99, and 10.88 ms per frame in `MainContentSystem`. Rendering takes 1.29 ms and post-layout takes 0.51 ms. This identifies the system, not the function responsible. No new run or binary change was made for this audit.

Neighboring existing logs provide useful context:

| Case at 100% | Main content average | Whole-frame p99 |
| --- | --- | --- |
| Split, before-side selection | 10.84 ms | 18.00 ms |
| Split, after-side selection | 10.88 ms | 20.47 ms |
| Unified diff | 10.50 ms | 19.64 ms |
| Working-tree source | 1.10 ms | 6.39 ms |

The [fixture](../tests/selection_extent.py) creates 4,999 numbered lines plus a final line, all changed between staged and working versions. It selects through the file, changes font size, resizes, copies, and then measures steady frames. The similar before/after averages make a side-specific selection defect less plausible than common diff work. These runs are context, not a controlled experiment.

The strongest source candidate is full-hunk preparation before viewport rejection in `render_sbs_hunk`, [diff_renderer.h](../src/ui/diff_renderer.h). Lines 1759–1771 scan every line for move and newline flags. Lines 1849–1866 scan again, copy line content, and allocate deletion/addition rows. Each paired row computes both wrap-cache lookups and changed ranges at 1778–1786. Culling happens later, at 1804–1807. Thus 484 live entities can coexist with thousands of prepared rows every frame. [DiffMetricsCache](../src/ui/diff_metrics.h) also returns the wrap vector by value on a cache hit, so a warm cache does not remove per-row key construction, lookup, or vector copies.

The source viewer already caches cumulative row counts and skips work for offscreen lines at `diff_renderer.h:1091–1136`. Split rendering returns through its separate path at 1080–1083 before that optimization. Unified diff also prepares wraps for all rows, and computes `hunk_ranges` at 1113 every frame.

A minimal first experiment is to compute paired `changed_ranges` lazily after the visibility check, once per pair with at least one visible fragment. That removes offscreen text comparison without changing pairing, wrap heights, entity IDs, anchors, or selection. Add a temporary call counter or scoped timer, then replay this exact fixture against the preserved baseline. Expected proof is a drop from all paired lines to visible paired lines, identical clipboard bytes and geometry, and improved main-content time. The size of the timing improvement is unknown.

The larger follow-up is cached split-row pairing and cumulative wrapped heights keyed by file render identity, hunk identity, width, font, and whitespace mode. Reuse the source-view approach to skip offscreen preparation while retaining logical anchors and Find destinations. Keep cache accounting bounded. Test resize, font changes, folds, local context, asymmetric additions/deletions, and content replacement before relying on those cached heights.

Two other costs deserve separate timer scopes. [hunk_signature](../src/ecs/components.h) at line 204 hashes every hunk byte; review progress, `file_reviewed`, and `render_hunk` can call it again on unchanged frames. A file-signature cache exists, but these callers still reach the uncached hunk helper. Selection highlighting loops over mounted `lastLines`, and keyboard code returns before constructing all code lines when no motion exists at `diff_renderer.h:596`. Syntax annotation has a plain-text early return in [hunk_syntax.h](../src/util/hunk_syntax.h), and token lookup occurs after culling. Those paths are weaker explanations for this plain `.txt` fixture than repeated row preparation and review hashes.

The unchanged final selection replay passed all three zooms and copy-limit checks.
Its 100% split-after case measured 14.54 ms p99 and 9.92 ms average main-content
work. The earlier miss remains recorded; the proposed optimizations still need
a controlled before/after experiment.

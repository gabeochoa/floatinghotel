# floatinghotel and px0 on the same journeys

On the predeclared journey, warm Quick Open at 100% on the fixture, floatinghotel's p95 is
20.8 ms against px0's 101.2 ms (n = 120 each): ratio 0.21, bootstrap 95% CI 0.16-0.30, so the
whole interval sits below the 0.80 required for a 20% win. floatinghotel passed all nine stress
correctness cases; px0 showed a mixed-version file after a rewrite between pages. px0 is faster
at switching between open tabs and at find-as-you-type on a far page; floatinghotel uses about
1/17 of px0's resident memory, most of which is Chrome.

Run of October 6, 2026 (13:01 UTC): floatinghotel `45b3ecd` plus this branch's uncommitted
changes, binary `74bf2e253d09`; px0 `5743e84` 0.1.1; Chrome 154.0.8037.98; macOS 26.6.2 on a
Mac16,12 (Apple M4). 39 fixture launches per app (30 at 100%, 3 each at 140%, 200% and a 900 px
window), 5 repetitions each, 20 real-repository launches per app, 3 memory runs per app. No launch
failed; two single px0 samples timed out (one warm tab switch on the fixture, one warm Quick Open
on Flask). Nothing else ran during the run except one low-priority Swift compile (`nice -n 19`, under a
minute); endpoint security agents held the load average between 8 and 20. Full tables: [report](triage-evidence/px0-compare-20261006/report.md),
[summary](triage-evidence/px0-compare-20261006/summary.json),
[metadata](triage-evidence/px0-compare-20261006/metadata.json).

## Fixture, 100%, milliseconds

| journey | fh cold p95 | px0 cold p95 | fh warm p50 | fh warm p95 | px0 warm p50 | px0 warm p95 |
|---|---:|---:|---:|---:|---:|---:|
| tree_open | 18.6 | 134.9 | 6.3 | 14.6 | 55.3 | 123.4 |
| quick_open (predeclared) | 21.3 | 75.1 | 14.0 | 20.8 | 51.1 | 101.2 |
| tab_a | 23.6 | 8.4 | 14.5 | 28.6 | 5.2 | 9.1 |
| tab_b | 21.9 | 8.7 | 13.2 | 28.6 | 5.5 | 9.8 |
| tab_a_again | 24.0 | 9.6 | 12.6 | 25.4 | 5.9 | 10.9 |
| back | 17.8 | 27.3 | 8.5 | 18.3 | 25.8 | 30.8 |
| open_big (400k lines) | 54.8 | 166.5 | 40.7 | 56.3 | 90.0 | 144.3 |
| find_later_page | 431.1 | 267.4 | 349.7 | 437.7 | 207.0 | 282.4 |
| markdown_preview | 19.3 | 56.3 | 8.5 | 15.3 | 17.0 | 53.9 |
| search_to_line (fh UI only) | 12.3 | - | 5.7 | 14.2 | - | - |
| search_api (px0 server only) | - | 251.1 | - | - | 14.8 | 171.7 |

px0 times include its own 140 ms find debounce. floatinghotel's find re-searches the whole
150,000-line file on each keystroke (about 350 ms); its tab switch re-decodes the page in its
one-active-document design. Both are the larger gaps worth work.

## Supplement: Forward, distant lines, far-page syntax

A second run (10 launches per app at 100%, 5 repetitions, same binaries) added the journeys
the main run lacked; [report](triage-evidence/px0-compare-20261006-supplement/report.md).

| journey | fh warm p50 | fh warm p95 | px0 warm p50 | px0 warm p95 |
|---|---:|---:|---:|---:|
| back | 13.6 | 27.6 | 25.3 | 31.5 |
| forward | 15.5 | 28.9 | 25.3 | 27.4 |
| goto_line_far (line 300,000 of 400,000) | 541.0 | 697.3 | 51.2 | 67.5 |
| far_page_text (line 59,990 inside one block comment) | 71.1 | 116.5 | 44.3 | 53.2 |
| far_page_syntax (comment colour on that page) | with the text | | 44.4 | 53.2 |

floatinghotel's go-to-line on a working-tree file re-reads from the start to count lines, since
working-tree bytes are never cached; that is the largest gap measured. Both apps show the far
comment page with the right colour in the first frame that shows its text: px0 highlights the
chunk exactly on the server, floatinghotel carries lexical state into each page. Its warm Quick
Open in this run, n = 40 per app, had p95 42.4 ms against 135.4 ms, ratio 0.31 with a 95% CI of
0.07-1.55: too few samples to decide. The predeclared test is the main run's n = 120.

floatinghotel's owned content (the active document payload) peaked at 467,805 bytes across the
main run's fixture journeys; its blob and patch caches stayed empty there because working-tree
bytes are never cached.

## Real repositories, warm, milliseconds (p50 / p95)

| repository | journey | floatinghotel | px0 |
|---|---|---:|---:|
| Flask 3.1.0 | quick_open | 20.2 / 24.5 | 77.1 / 153.7 |
| | tab_a | 11.2 / 21.4 | 4.5 / 6.6 |
| | open_big (`CHANGES.rst`) | 18.2 / 24.4 | 58.4 / 97.5 |
| Redis 7.4.1 | quick_open | 42.9 / 53.1 | 67.4 / 113.7 |
| | tab_a | 37.4 / 54.1 | 6.6 / 7.9 |
| | markdown_preview | 23.8 / 46.8 | 12.1 / 23.6 |
| React v19.0.0 | quick_open | 12.2 / 19.3 | 25.2 / 45.3 |
| | open_big (`ReactFiberWorkLoop.js`) | 38.1 / 51.0 | 50.2 / 102.2 |
| TypeScript v5.7.2 | quick_open | 38.6 / 55.8 | 118.5 / 170.8 |
| | tab_a | 43.1 / 50.0 | 4.6 / 6.5 |
| | open_big (`checker.ts`, 52,788 lines) | 35.5 / 50.6 | 54.5 / 89.9 |

Every journey for every repository is in the report.

## Memory, MB of summed RSS (3 runs)

| phase | floatinghotel | px0 (server plus Chrome) |
|---|---:|---:|
| start | 56-57 | 1,273-1,291 |
| 20 tabs open | 69-72 | 1,576-1,628 |
| after scrolling 400k lines | 95-100 | 1,469-1,596 |
| 30 s idle after closing all | 64-79 | 1,122-1,330 |
| peak | 96-100 | 1,614-1,715 |

px0's own server is 17-78 MB of that; Chrome's renderer (549-889 MB) and helpers make up the rest,
across 10-13 processes. floatinghotel's physical footprint at idle, including GPU memory, is
121-150 MB.

## Frames

floatinghotel CPU frame time, per-launch p99 over 1,000 frames: median 11.2 ms, worst launch
264.6 ms (one 100-frame block in one launch). px0 animation-frame interval p99: median 18.6 ms,
worst 19.4 ms. These measure different things and are not compared.

## Stress correctness

| case | floatinghotel | px0 |
|---|---|---|
| rapid A/B/A opens | pass | pass |
| superseded query | pass | pass |
| closing during loading | pass | not applicable: px0 creates a tab only after its first chunk |
| file rewritten between pages | pass (keeps old rows, shows a reload notice) | fail: far rows from the new file, top rows from the old, in one document |
| same size, same mtime rewrite | pass | pass |
| copied bytes | pass | not run |
| reopen after cache churn | pass | pass |
| far page of a long comment | pass | not run |


## Method

`tests/px0_compare/run.py` drives both apps through the same journeys and writes every sample,
failure, memory timeline and the report. Run it on a quiet machine after building:

```sh
HOME=/tmp python3 tests/px0_compare/run.py --output output/px0-compare-NEW --px0 /path/to/px0 \
    --repos-dir output/px0-repos --launches 30 --reps 5 --zoom-launches 3 --real-launches 5 --memory-runs 3
```

| part | file | what it does |
|---|---|---|
| fixture | `tests/px0_compare/fixture.py` | generated repository: small sources, a 400,000-line file under 64 MiB, a 150,000-line Find target under px0's 8 MiB search cap with a decoy on line 100, long lines, Unicode, CRLF, Markdown links, a search token at `src/search_target.cpp:77`, 20 module files |
| px0 driver | `tests/px0_compare/px0_driver.mjs` | starts px0 (`-no-open -no-lsp -quiet`) and headless Chrome with a temporary profile over CDP; emulates zoom through viewport metrics; times from the input event reaching the page to the first animation frame showing the destination |
| floatinghotel driver | `tests/px0_compare/fh_driver.py` | headless E2E script; `reading_probe` times from input dispatch to the frame showing the destination, and its optional line argument requires that line on screen |
| runner | `tests/px0_compare/run.py` | records versions, binary hashes, Chrome, OS, machine, display; alternates app order every launch under `nice -n 10`; every launch is a fresh process |
| memory | `tests/px0_compare/memsample.py` | resident set of every process each app uses, every 250 ms, through 20 tabs, a long scroll, closing everything and 30 s idle |
| stress | `tests/px0_compare/stress.py`, `px0_driver.mjs --journey stress` | correctness under rapid opens, superseded queries, closing during loading, a file rewritten between pages, a same-size same-mtime rewrite, copied bytes, cache churn, a far page of a long comment |

Both drivers measure CPU completion, not presentation. Warm samples are the second and later
repetitions inside one launch; tabs are closed between repetitions, so a warm open is a reopen.
px0 evicts a closed file's content, so its warm opens fetch again; floatinghotel's reopen can hit
its bounded caches. That is the intended comparison of reopening a closed tab, not repeated HTTP
requests.

## Journeys and what is shared

| journey | input | destination | both apps |
|---|---|---|---|
| tree_open | click `alpha.cpp` in the file tree | line 1 visible | yes |
| quick_open | Cmd+P, query, Enter | `beta.cpp` line 1 | yes; predeclared head-to-head |
| tab_a, tab_b, tab_a_again | Ctrl+Tab with two tabs | the other file | yes |
| back | Alt+Left | previous file | yes |
| open_big | Quick Open the 400,000-line file | line 1 | yes |
| find_later_page | last keystroke of a find-as-you-type query | line 120,000 (from the decoy at 100) | yes |
| markdown_preview | toggle preview on README.md | rendered preview | yes |
| search_to_line | click a repository-search result | `src/search_target.cpp:77` | floatinghotel only |
| search_api | `/api/search` from the page | JSON result | px0 server only |

px0 0.1.1 (`5743e84`) removed its search-in-files input (`6d5a60a`, "Simplify sidebar layout")
while keeping the shortcut, so repository search through its UI is unreachable; its server call
is timed separately and is not comparable. px0 searches and finds only in files under 8 MiB
(`searchFileCap` in `search.go`); the Find target is sized under that cap. floatinghotel's repository
search runs `git grep` over the working tree or a revision with a 5,000-match / 4 MiB capture
limit; px0 walks its own index with `.gitignore` exclusions. Search speed is therefore not compared.

Frame times: floatinghotel reports CPU time per frame over 1,000 frames on the 400,000-line file
with a long scroll before each 100. px0 reports animation-frame intervals while scrolling, which
the page can observe; Chrome does not expose per-frame CPU time to the page. They are shown side
by side and not compared.

## Limits

Cold-storage runs need the page cache purged (`sudo purge`); without root they are not run, and
cold means the first open in a fresh process with warm OS caches. Memory sums RSS per process,
so shared pages count once per process and px0's multi-process total is overstated. Real
repositories (Flask 3.1.0, Redis 7.4.1, React v19.0.0, TypeScript v5.7.2, shallow clones) run
the shared core journeys only; tree, Find and search stay on the fixture where both apps get the
same work.

# Reading priorities added during implementation

The user requested these changes after document tabs (step 03): an unstaged
review with feedback before committing, a separate staged view, quiet startup,
close buttons and tab menus, faster commit loading, a smaller commit header,
independent code-text sizing, correct zoom hit testing, saved window dimensions,
and wrapped long lines. These take priority over step 04; they do not mark later
numbered steps complete.

## Implementation

Unstaged changes uses the same review surface and feedback storage as a commit.
Staged changes has its own destination. New files are read on a worker with the
existing bounded reader. Incomplete previews remain visibly partial and cannot
count as fully reviewed. NUL-delimited status records preserve Unicode, quoted,
and newline-containing paths; nested untracked directories list their files.

Each document tab has a close control and a menu bound to the clicked document.
Cmd+W closes a document; Cmd+Shift+T restores from the 20-entry closed history.
A closed originating review remains available to a source excursion. The old
automatic legacy-review notice is removed; existing review data remains stored.

Commit reads resolve identity with one Git process on cache hits. Cached patches
include immutable metadata and distinguish root/shallow, first-parent, and
explicit-parent comparisons. The title uses a 20-pixel font and at most two
lines; metadata scrolls with the changes while subject and revision stay visible.
The full commit message remains available through disclosure and virtualization.

Code text defaults to 17.6 pixels instead of 16. Cmd± and Cmd+0 change/reset code
text; menu and pinch zoom remain UI zoom. Divider dragging converts the UI
pointer to logical coordinates once. Hover diagnostics use visible rendered
rectangles and record the resolved hover target.

Long lines use measured visual fragments in unified, split, and full-file views.
Each fragment retains its source line, side, and byte offset. Continuation rows
omit repeated line numbers; copying joins fragments without adding newlines.
Syntax colors come from the complete source line. The wrap cache shares the
existing metrics budget, and offscreen fragments use viewport culling.

Startup reads saved dimensions before native window creation. Normal shutdown
saves current dimensions, dock state, sidebar width, and expanded width. The
restart test uses an isolated settings directory.

## Verification

175 unit checks passed: settings 20, diff tools 27, bounded reader 22, status
parser 53, patch/cache 6, navigation state 24, review storage 20, and metrics
cache 3. Native checks passed wrapping and selection in unified, split, and
source views at 100%, 140%, and 200%; compact-close pixels and full hit targets;
commit title spacing; header scrolling; independent text sizing; tab menus,
close/reopen; unstaged/staged feedback without index changes; and actual window
restarts with dock dimensions and the sidebar split retained. Hidden native
resize probes passed at standard and Retina scale. Seven navigation regressions,
long-message disclosure, new-file/symlink navigation, and whitespace rendering
also passed.

The final binary is
`dee93199d3de443cf7f4e4d356eb75036d392bdd43ac5366cb7105f469aaa8d1`.
Geometry captures use build 9; build 10 adds untracked-read readiness, and build
11 corrects Untracked sidebar selection. The final binary reruns affected
navigation and the 90-sample reading journey. Each archive records its binary
hash. Reproducible runners and their original paths are recorded in
`docs/reading-navigation-evidence/priorities`.

The completed reading run measured selection feedback at 9.26 ms p95 and
10.97 ms maximum across 90 samples. All nine render runs passed the 20 ms p99
gate, ranging from 2.52 to 16.96 ms. Estimated owned active content peaked at
15,245 bytes, excluding caches, workers, allocator metadata, and GPU memory.
Existing cache budgets are unchanged. The 4,000-line source test rendered 172
entities and passed at 6.91 ms p99 in the integrated replay.

Cached commit loads ranged from 121.34 to 205.40 ms, with a nine-sample p95 of
205.40 ms. Cold commit loads ranged from 397.48 to 2,206.72 ms. Twenty-seven of
36 warm historical/review destinations exceeded 100 ms. Reducing Git processes
on a patch-cache hit from three to one has not met the 100 ms switching target.
Earlier runs had cached commit loads up to 676.76 ms and render p99 failures of
24.19 and 32.20 ms. Those failures remain archived; the passing run does not
erase the observed variability. Steps 52–54 and final acceptance must address
remaining loading latency.

The tab × now has transparent idle fill, a centered 20-pixel hover treatment,
and a full-height click target. The commit-row gap changed from 8 to 2 logical
pixels while graph coordinates stayed fixed. The default code font is 17.6
instead of 16; Cmd+0 resets to that default. The removed startup notification
was already suppressed in test mode, so its removal is verified at its call
site rather than claimed as a reproduced native before/after toast test.

## Failures retained

- The first priority journey showed no staged files because the fixture only
  loaded the unstaged patch. The fixture now loads `git diff --cached` too.
- A cancellation test missed its 5-second startup-marker deadline during severe
  host slowdown. The retained failure is followed by a passing six-test rerun
  in `output/priority-polish/patch-verified-tests.log`.
- The new reader integration test initially omitted the parser dependency from
  its link command. The runner now links it.
- The new NUL-status test was initially inserted into a diff fixture string.
  It was moved to the test declarations.
- The restart replay exposed feedback-scope loading resetting the expanded
  reader. Switching feedback storage now preserves the current reading-panel
  state instead of loading a saved panel-visibility flag.
- The first geometry-driven text replay did not scroll far enough to reach the
  last source line at increased zoom. It now scrolls to the end and asserts the
  target lies in the viewport before injecting a drag.
- Build 3 exposed non-inline color references when layout diagnostics included
  the diff renderer in a second translation unit. The references are now inline.

- A 200% selection replay caught the file header growing during a selection.
  Narrow headers now reserve action height; forward and reverse drags assert
  stationary code.
- The first compact-close pixel check caught implicit button padding shifting
  the glyph. All four padding sides are now explicitly zero.
- Large-page rendering initially missed the cache entirely and reached
  43.85 ms p99. Redistributing the existing 5 MiB metrics budget reduced average
  frame time from 31.31 to 9.73 ms, but p99 remained above the 20 ms gate.

- The combined feedback replay captured three tracked files before two new-file
  previews arrived. The native refresh gate now waits for the matching repository
  and data generation to finish those previews. The app also counts that worker
  as pending work. The fixture no longer relies on a 20-frame delay.

- The older metrics replay exposed navigation resetting the Untracked sidebar
  to Changes after opening a new file. Working-review navigation now chooses
  the sidebar section from the actual file status; historical and source
  destinations leave that section alone. The same replay visits a second new
  file, including a dangling symlink, to verify the list stays reachable.

- Reading timing on build 11 hit 24.19 ms render p99 at 140%; a repeat hit
  32.20 ms at 100%. Both failures are retained. Host process snapshots showed
  concurrent compilation, indexing, and security scanning. That is context, not
  proof of the cause; the 20 ms gate remains unchanged. Cached commit loads in
  these runs also exceeded 100 ms. These results do not establish the final
  performance acceptance target.

- The legacy All Files test expected loaded content after only screenshot-delay
  frames. Under host load it captured the correctly identified loading state.
  It now waits for the file read before asserting Complete file.

See `docs/afterhours-gaps.md` for the requested upstream audit and subsequent
framework limitations/workarounds. Source-position selection across reflow,
full tab overflow behavior, and the rest of steps 04–60 remain outstanding.

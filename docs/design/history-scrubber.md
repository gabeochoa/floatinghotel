# History scrubber

Exploration, for decision. Recommendation: build the scrubber as a range selector over the commit
log that sets the existing revision comparison (`A..B`), with the saved review baseline drawn as a
fixed marker on the same track and never moved by scrubbing. Everything it shows already exists as
a comparison or a review snapshot; the new part is one control and the "arrived since" highlight.

## What it is for

A reviewer comes back to a branch that moved. Today they can compare two revisions (Compare
Revisions, parsed by `parse_revision_range`), open a commit, or open the review snapshot
(`review_snapshot.cpp` keeps `head` from "Save review baseline" and diffs the working state
against it). None of these answers "show me what arrived since I last looked, one step at a time"
without typing revisions.

## Shape

A horizontal track above the commit log, one tick per commit on the current branch's first-parent
chain, newest on the right.

| element | meaning | moved by scrubbing |
|---|---|---|
| left handle | comparison base `A` | yes |
| right handle | comparison target `B` | yes |
| baseline marker | the saved review baseline (`head` in the snapshot) | no |
| tinted ticks | commits after the baseline ("arrived") | no |

Dragging a handle, or pressing `[` / `]` with a handle focused, sets the comparison to `A..B` and
opens it like Compare Revisions does: same document kind, same `navigation::open` path, so Back
and Forward, tabs and reading anchors already work. Releasing on the baseline marker snaps to it.

## Keeping three things distinct

1. Precise navigation stays on full commit ids. The handles resolve to SHAs at release, the
   document title shows both, and the reading anchor keys on `(path, revision, side, line)` as it
   does today. A branch moving under the scrubber does not move an open comparison.
2. The baseline is the review's, not the scrubber's. Scrubbing never writes the snapshot;
   "Save review baseline" stays the only writer.
3. "Arrived" is computed from the baseline: `git rev-list <baseline>..HEAD --first-parent`. With no
   baseline there is no tint.

## Cost and limits

The track needs the first-parent commit list, which the commit log already loads in pages of 100
(`commits loaded: 100 commits`). Ticks past the loaded page draw as a collapsed segment that loads
the next page when a handle enters it. Merge commits show as one tick (first-parent); their
second-parent history is reachable by opening the merge.

Each scrub release costs one comparison read (`git diff A B`, cached by the commit patch cache key
of repository, parent and commit). Intermediate positions while dragging show only the tick
labels, not diffs, so a drag across 200 commits issues one read.

## Rejected alternatives

1. Animated playback of diffs commit by commit: shows each commit, not the cumulative change a
   reviewer acts on, and needs a timing control nobody asked for.
2. A file-level time slider (blame scrubbing): `file_history.h` already lists a file's commits;
   this belongs there if anywhere, and it does not answer "what arrived on the branch".

## Decisions needed

1. Whether the track sits above the commit log (recommended) or in the comparison document header.
2. First-parent only (recommended) or all commits.
3. Whether to ship the "arrived since baseline" tint without the handles first; it is the part
   that needs no new navigation.

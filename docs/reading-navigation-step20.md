# Step 20: reveal the active file in its tree

Explicit navigation reveals the destination once per workspace navigation
generation. It expands only that file's ancestors and scrolls its row into
view without moving focus out of the reader. Code scrolling updates reading
anchors without creating another tree reveal. Manually collapsing a folder
also leaves it collapsed until another explicit navigation requires it.

Review trees use their own revision and retained file summaries. Opening the
before side of a renamed file reveals its current review-row identity.
Working and index trees accept only destinations from their own scope;
historical files do not become working-tree rows.

The native journey exposed a call-site identity mistake: separate `mk` calls
for reveal and virtual-list construction produced different entities. The
list handle is now created once and passed to both operations. This also
allows type-to-select to jump beyond the currently rendered rows. Workspace
checkpoints now include pending reveal paths, focus state, and generations.
The framework integration gap is recorded in `docs/afterhours-gaps.md`.

All 12 tree unit tests and 56 navigation/review-target tests passed.
The native reveal journey passed at 100%, 140%, and 200% zoom, including
historical renames, collapsed ancestors, offscreen rows, focus ownership,
manual tree scrolling, empty filtering, and an unchanged Git index and diff.
Its 120-frame p99 samples were 8.80, 8.40, and 5.11 ms respectively.

The first keyboard regression timing sample exceeded the 20 ms gate
(42.42 ms p99, 65.89 ms maximum). Other processes were consuming substantial
CPU at the time; that observation does not establish the cause. The failed
run remains in `output/step20-tree_keyboard`. The final keyboard replay
passed at all three zooms, with p99 samples of 9.92, 11.26, and 8.68 ms.
Its source-origin assertion now follows the source that was actually opened:
at 200%, the first visible Open file action belongs to the next file.
Type-to-select, focus return, and source-origin regressions also passed at
all three zooms. Precise history journeys passed at all three zooms, and
all seven navigation regressions and the ownership check passed.

The first reveal attempt exposed the separate list handle problem. Reusing
the handle for an empty, nonvirtual panel then retained virtual content size;
empty panels now have their own identity. Two later test corrections use
the open-tabs menu for an overflowed tab and a sufficiently large gesture
to reach the top at 200%. Those attempts remain in `output/step20-reveal-*`.

Evidence: `docs/reading-navigation-evidence/step20`.
Full successful journey: `output/step20-reveal-complete`.

Binary SHA-256: `61da0306cc061b48a0b9635285733f3fa46c551db59d262f1c124b95ee65118b`.

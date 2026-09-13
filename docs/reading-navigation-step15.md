# Step 15: shortcuts follow focus

Reader commands now require focus in the active document tab or its code
region. Text inputs and text areas own editing keys through their inner fields
and ancestor state. Back and Forward remain available from other non-editing
controls, including the repository tab after an inactive document closes.
Temporary panels and menus block shortcuts for the reader underneath.

Picker arrows and Enter, Find Enter, and search submission run only from their
own input. Code-copy shortcuts ignore text fields. Review approval and comment
shortcuts use the same availability conditions as their visible controls.
Feedback export requires focus in the feedback panel outside an editor.

Seven focus unit tests passed. `tests/shortcut_focus.py` passed at 100%, 140%,
and 200%, checking Option+Arrow in Quick Open, Find, search, and comment text;
select-all followed by replacement; Find Enter ownership; inactive review
actions; and working-review hunk navigation. The replay checks that staging
and working file contents do not change. Precise history, focus return, and all
seven navigation regressions passed. All measured frame p99 gates stayed below
20 ms; individual results are in `frame-metrics.json`.

The before-change replay in `output/step15-baseline` navigates away while
editing Quick Open. The first history regression caught Back being blocked
after an inactive tab closed; the final replay covers that correction. The
first shortcut replay also attempted clipboard assertions in the windowless
runner, which has no initialized system clipboard. These failures are retained
in `output/step15-shortcuts-first`; clipboard contents are not claimed verified.
The reusable backend gap is recorded in `docs/afterhours-gaps.md`.

Normal commit reviews still lack the unified hunk cursor scheduled for step 42.
The working-review journey exposed an existing composer that can be partly
above the scrolled viewport; keeping comment entry visible remains part of
steps 42 and 59. This step verifies command ownership, not those later changes.

Evidence: `docs/reading-navigation-evidence/step15`.
Full replays: `output/step15-verified-*`.
Binary SHA-256: `23dc9c0af0e8c72ced8b51014fba12dd034e6a58c7b78c2a55223b222d7217f1`.

# Step 14: focus return to the invoking control

Focus return passed the native replay and targeted regressions.

Focus return stores a repository, document, region, logical item, and control
name. Menus, pickers, Find, search previews, and feedback panels keep their
invoking target. The app resolves that target against the current UI after
layout, so the input or row may have a different entity ID. Navigation and
repository changes invalidate pending returns. Separate repository tabs also
clear the return stack when they display the same repository path.

Repeated tree, history, and search rows use paths, commit IDs, or match
locations. Closing a document cannot restore focus to one of its controls.
A missing control falls back within its visible region. Text-input focus uses
the composite widget's inner field while preserving the outer input's name as
its identity.

`tests/unit/test_focus_target.cpp` covers semantic return, nested popups,
navigation/repository changes, and missing documents. `tests/focus_return.py`
exercises mouse and keyboard journeys at 100%, 140%, and 200%, with screenshots
and semantic focus assertions. It also types directly into Quick Open and Find
without a preceding click, dismisses a picker from the tree filter and history,
closes a tab that invoked a menu, and switches repository tabs with a picker
open. Its working-review feedback journey leaves both the staged and unstaged
file contents unchanged.

Five unit tests passed. The seven navigation regressions, source-origin replay,
close/reopen replay, all 13 anchor-layout checks, and recent-tab replay passed.
Focus replay frame p99 values were 1.95, 1.67, and 2.56 ms at 100%, 140%, and
200%. All recorded frame gates remained below 20 ms. Binary SHA-256:
`743a568f441a9658297407d1a3576385075ec27cb244bc7c55ee25fe076ba7a5`.
Evidence is in `docs/reading-navigation-evidence/step14` and full captures in
`output/step14-verified-focus_return` and `output/step14-verified-recent_tabs`.

The first replay exposed the outer-input focus bug. The second exposed a stale
`MenuBack` action: raw Escape handling dismissed a panel without consuming the
mapped action, so a later text field blurred. The dismissal handler now consumes
that action. A fifth unit test caught pending focus surviving navigation while
waiting for layout; pending returns now retain and check their generation.

The recent-tab regression still compared pixel offsets from before step 13.
Its saved line was within 0.001 pixels of the correct viewport fraction despite
a different bounded page offset. The assertion now checks the logical position.
The initial failure remains in `output/step14-regressions.log`; the updated test
passed at all three zooms in `output/step14-final-replays.log`.

Early feedback test attempts selected the inactive working-changes document
without opening its review. The final journey uses the visible Unstaged action,
adds a real comment, then exercises the feedback panel's close and reopen path.

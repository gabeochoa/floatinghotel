# Step 56: compact dock review inbox

The dock's changed-file heading now shows an actionable remaining-file count.
Clicking it opens the first unreviewed file in the current review, including
when a related source document is active. It uses the existing revision-scoped
review progress and source-origin summaries. Hover leaves the dock collapsed.
Once all files are reviewed, the heading shows the ordinary complete count.

Empty dock reviews omit the unused file filter. Changed files remain above
commit history; the footer remains fixed. The count uses the existing heading
row, adding no vertical chrome. Marking a file reviewed no longer shows a
redundant toast over dock history; its checkmark and count confirm the action.

Final binary: `4b439113144c1b7eecd4ee8f6e4a1e8b4da6eb1e11819a27c1063d63fd7e5cb2`.

Native and offscreen journeys passed at 100%, 140%, and 200%. The tests follow
three remaining files through approval to completion, open the correct next
file from both review and source, verify hover does not expand, and check row
click expansion and the empty staged view. Geometry checks cover heading
height, clipping, changed-files/history ordering, and the fixed footer. Native
p99 samples: 1.68, 1.52, 1.40 ms, below the 20 ms gate.

Dock reading state and selected-file/all-file progress, feedback, staging
identity, narrow geometry, and persistence regressions also passed at every
zoom. Git remained unchanged. Screenshot inspection caught the redundant
approval toast; the final replay checks its absence. The before image is
retained alongside final images.

No new Afterhours limitation was encountered. The isolated native-window
workaround from step 55 is reused. Evidence contains 20 artifacts and
2,658,499 bytes; compressed members and PNG signatures verified.

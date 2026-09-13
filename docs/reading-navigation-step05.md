# Step 05: recognizable document titles

Source tabs show filenames and the shortest distinguishing parent suffix when
another open path has the same filename. Revision badges distinguish historical
and index sources; a working-tree badge appears when another revision of that
path is open. Short object IDs grow when their prefixes collide. Tooltips retain
full paths and revisions.

Commit tabs retain their subjects in lightweight document metadata. Navigation
seeds known subjects from history/search; accepted patch metadata fills missing
subjects. Duplicate subjects show hashes. Different merge-parent destinations
also identify their parent. Comparison tabs identify both endpoints. Preview,
keep, close, and context-menu behavior are preserved.

Ten title tests and 35 navigation
tests passed. They cover root paths, Unicode labels, duplicate parent suffixes,
working/index/historical copies, hash collisions, subjects, merge parents,
comparison endpoints, and subject retention through alias resolution and reopen.

The native title replay passed at 100%, 140%, and 200% zoom. It opens duplicate
filenames and historical/working-tree copies, switches them with the mouse,
and returns to the retained commit. It checks exact identities, lightweight
inactive documents, screenshots, visible rectangles, and measured label fit.
Render p99 was 3.73/5.67/3.08 ms. Preview/keep and compact close-hover replays
also passed at all three zooms; commit-row hover and spacing remain intact.

The first title replay passed destination and rectangle checks, but visual
inspection found the parent-path text was ellipsized. The corrected width
calculation reserves icons, gaps, close targets, and badges. Title labels use
an explicit zero text inset. Layout dumps now include measured text widths;
the replay requires enough room for the complete short labels and badges.
The original screenshot is retained as `before-label-fit.png`.

The final binary SHA-256 is
`9f7f87b1b4b49ade4907417d709182319e3e6f40f4ef2f52e07eb8932cda93f4`.
Evidence is in `docs/reading-navigation-evidence/step05`; full runs remain under
`output/step05-native-final`, `output/step05-preview-final`, and
`output/step05-hover`. Run `tests/document_titles.py --output NEW_DIRECTORY`
to repeat the title journey. Overflow in narrow windows is step 06.

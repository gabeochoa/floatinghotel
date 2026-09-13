# Step 57: return to a saved review

Saving a baseline is explicit. Entering working review no longer captures or
rewrites it. Saved contents carry their HEAD identity and capture time, and
successful capture publishes that metadata with the baseline path. Failed
capture leaves the previous baseline intact.

The working-review header puts Since last review first, beside the saved
identity/time. Replacement is an explicit overflow-menu action. A native View
menu action opens the saved comparison from any document, including source.
The comparison uses saved bytes and leaves the underlying document's selection
and reading anchor alone. Legacy baselines show an unavailable capture time.

Final binary: `44c9b425c0f73686208fbb4ecdf57e3c1eb2396e6e96f43eed0e17a5a697bd9f`.

All 22 snapshot/store unit checks pass, including metadata persistence, saved
byte comparison, deleted/new/binary files, and failed replacement retaining the
old baseline. Native and offscreen journeys pass at 100%, 140%, and 200%:
explicit capture, restart, unchanged baseline bytes during comparison, saved
revision/time labels, missing-baseline errors without recapture, explicit
replacement, and source selection/anchor restoration through Close and Escape.
Native p99 was 2.80–3.29 ms; offscreen p99 was 2.02–4.99 ms. Git's index stayed
unchanged. Selected-file review, Escape, and the older snapshot regression pass.

The first replay exposed test mode's disabled persistence; the guarded isolated
persistence option now exercises production save/load. Another assertion used
retained source row metadata to inspect the separate snapshot view; it now
checks rendered snapshot lines, and the view clears obsolete selection rows.
The older regression expected the former combined path/count label; its
assertion now checks the separate labels. These attempts are retained.

The saved-content overlay exposes whole-diff copy. Character selection remains
with the retained source/review document; copying ranges directly from saved
snapshot bytes would require a snapshot source destination.

Inspected images and scripts/layouts/logs are in
`docs/reading-navigation-evidence/step57`: 25 artifacts, 3,638,954 bytes.
All compressed members and PNG signatures verified.

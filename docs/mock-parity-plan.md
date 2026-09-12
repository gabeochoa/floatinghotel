# Review-focus parity pass

- [x] Read the Poteto principles and the parity mock.
- [x] Frame the scope and verification contract.
- [x] Capture a representative native commit and source view before changes.
- [x] Record at least 50 distinct mismatches against the mock and current user overrides.
- [x] Fix the frame background and verify pixels.
- [x] Style the file tree and verify expansion, selection, and clipping.
- [x] Align commit heading, metadata, toolbar, and diff cards with the mock.
- [x] Align sidebar chrome, tabs, and footer without changing the user's layout choices.
- [x] Verify normal, enlarged, and narrow layouts and nearby behavior.
- [x] Audit the decision trail and obtain cross-model review.

The reference is `docs/mocks/review-focus.html`. Later user choices take priority:
files above history, single-line commits without authors, native macOS menus,
and the larger code font. Fake mock identity and decorative search controls are
not requirements.

Done means at least 50 evidenced discrepancies are addressed, each linked to its
implementation and a native screenshot or measurable check. Record any unresolved
framework limit honestly in `docs/afterhours-gaps.md`. Do not claim pixel-perfect
parity for different content or native window chrome.

Use roughly six coherent implementation units with local commits. Each unit
gets a build, focused behavior checks, and visual inspection before proceeding.
The first checkpoint is the reproduced background-strip failure and corrected
capture. Root owns all production edits and builds. The visual auditor is
read-only. Native tests run sequentially because their fixture path is shared.

No new source comments, no pushes, no visible app launches. Commands use nice.
Decisions are appended to `docs/mock-parity.tsv`.

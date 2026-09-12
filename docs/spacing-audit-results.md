# Review UI spacing audit

Measurements are logical pixels unless stated otherwise. Screenshots and JSON
captures live under `output/spacing-audit/`; they are local review artifacts.

| Area | Current layout |
| --- | --- |
| Commit rows | 24 high, 4 outer horizontal margin, 4 inner left padding, 8 before metadata |
| Commit ages | Right aligned after badges, with one stable right edge |
| Sidebar boundary | Matches the sidebar's full height, including its header |
| Empty commit files | A 28-high label at the top of the file region |
| Main content | Existing 24 horizontal and 8 top inset retained |
| Feedback panel | 12 padding, 8 vertical gaps, 12 separation from the diff when docked |
| Feedback cards | 8 padding and gaps, 28-high actions, 6 corner radius |
| Toasts | 12 padding, 8 between cards, 28 dismiss targets, 12 above the 26-high footer |
| Small icon controls | Explicit zero padding instead of the framework's 16-per-edge fallback |
| Ordinary buttons | 8 horizontal padding in the shared preset |

The source gutter and graph lane spacing are intentional exceptions to panel
padding. Do not align code glyphs by moving selection geometry independently.
The immediate plain-label renderer still inserts five physical pixels when
drawing. Insets reported by JSON are requested configuration, not glyph bounds.

The Files sidebar now scrolls its upper controls when they cannot fit. Commit
history retains its assigned space. Some legacy Git controls still use
window-relative font sizes and look small at enlarged zoom. Their typography
needs a separate pass; this audit does not claim every legacy control has been
converted to logical-pixel sizing.

The stacked working review retains file and hunk navigation, approval, baseline
comparison, and commit browsing. Feedback has separate location and action
rows, an explicit close control, and one local Markdown/clipboard export.
Copying does not send feedback to a remote service.

Native macOS menus were exercised through windowless AppKit and the headless
app. Physical menu tracking and OS keyboard delivery through an open menu still
need a desktop check. The non-native fallback places menus above repo tabs.

Branch verification uses a disposable divergent history, a non-fast-forward
merge, both parent comparisons, and an empty commit. The broad legacy flow suite
also contains placeholder scenarios; its pass count is not proof of every
feature named by those scenarios.

Use `docs/spacing-audit.tsv` for final evidence paths and verification results.

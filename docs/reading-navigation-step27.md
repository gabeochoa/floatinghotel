# Step 27: Quick Open overlay, container hover, and native resizing

Quick Open keeps the active reader visible beneath a compact overlay. Opening,
filtering, scrolling results, and dismissing it preserve the reader's viewport,
selection, document identity, and navigation visits. Escape and outside clicks
return focus to the invoking control. The list uses logical-pixel typography,
zoom-aware virtualization, and the current viewport height when revealing a
selection after reopening or resizing.

The user's large gray hover blocks came from click-to-focus containers receiving
button hover tint. `bind_focus_region` now adds pointer focus without changing
the container background. Ordinary buttons and rows keep their own hover
feedback, including the history Retry button and compact tab-close controls.

The native resize callback now draws after updating dimensions. Programmatic
dock resizing keeps its existing between-frame queue. Window and layer
backgrounds are dark before presentation and after resize. The hidden native
probe reproduced the original missing-draw behavior, then verified matching
Metal drawable dimensions and GPU pixels for 16 size changes at native/Retina
scale. The existing dock probe passed 56 frames without mid-frame size changes.

Final UI binary: `2a9845609223d52aed22ded5498f3322e3eb837d55918da8fe9c6528a8e11cfb`.
The final picker, hover, and semantic-focus journeys pass at 100%, 140%, and
200%, including narrow windows and empty/populated staged and unstaged views.
All seven navigation regression scenarios and the navigation ownership checker pass on the final binary. PNGs, compressed layout/workspace
snapshots, scripts, and logs are in `docs/reading-navigation-evidence/step27`.

| Check | Final p99 |
| --- | --- |
| Picker, 100% / 140% / 200% | 5.26 / 4.11 / 4.32 ms |
| Empty/populated container hover | 3.45–5.14 ms |
| Focus return, 100% / 140% / 200% | 5.54 / 5.07 / 5.06 ms |

Native resize draw callbacks took 1.5–7.6 ms. The first two Retina window
operations took 21.6 and 33.6 ms overall; the remaining Retina samples were
7.6–13.1 ms. These are individual native-window samples, not full-app layout or
compositor timings. The probe runs real window setters in AppKit's tracking
loop with periodic drawing paused; it does not simulate a physical mouse drag.
The harness uses the system compiler after Zig stalled while linking the probe,
and links the same app-built Metal object.

The initial overlay replay caught a leaked input gate that suppressed focus
return. Later checks caught a 16-pixel modal-list overflow, unscaled font tiers,
and a selected row hidden after reopening in a shorter window. Those are fixed.
A fractional-anchor comparison now allows 0.00001 rounding error while checking
line, revision, side, column, rendered rows, and scroll offsets exactly. The
200% outside-click test uses the exposed tab edge; its center is under the popup.

One intermediate focus run had p99 27.27 ms and max 81.54 ms; subsequent focused
runs passed. One legacy navigation process timed out before writing its first
log line. Failed attempts and timing variability remain under `failures` and
`intermediate-regressions` in the evidence directory. Afterhours input-gate,
focus-region, typography, and native-resize limitations are recorded in
`docs/afterhours-gaps.md`.

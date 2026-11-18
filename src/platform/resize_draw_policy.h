#pragma once

namespace resize_draw {

// The synchronous windowDidResize draw runs a whole app frame on AppKit's
// resize path; a slow frame would otherwise serialize the entire drag
// (and any background wait inside it would stall the resize). At most one
// synchronous draw per 120 Hz frame; anything sooner is skipped and the
// regular frame loop paints it. Never blocks, never waits.
inline bool should_draw(double now, double lastDrawEnd, bool drawInProgress) {
    if (drawInProgress) return false;
    return now - lastDrawEnd >= 0.008;
}

}  // namespace resize_draw

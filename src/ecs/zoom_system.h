#pragma once

// Trackpad pinch and Cmd+= / Cmd+- / Cmd+0, all driving Theme::ui_scale.
//
// Pinch needs the macOS gesture monitor, which is opt-in at build time
// (-fblocks -framework AppKit -DAFTER_HOURS_ENABLE_MACOS_GESTURES) and
// installed once from main(). Without it get_pinch_delta() reads 0, which is
// the right answer on a machine with no trackpad -- the keyboard path still
// works either way.

#include <afterhours/src/plugins/input_system.h>

#include "../ui/zoom.h"
#include "ui_imports.h"

namespace ecs {

struct ZoomSystem : afterhours::System<> {
    bool should_iterate() const override { return false; }

    void once(float) override {
        // Compose the delta rather than tracking gesture boundaries: +0.01 is
        // "grow 1%", so this reads the same whether the gesture just started
        // or has been running for a second.
        const float pinch = afterhours::input::get_pinch_delta();
        if (pinch != 0.0f) ui::zoom::scale_by(1.0f + pinch);

        // afterhours::input, not graphics: only the former consults the e2e
        // injector, so raw graphics polling is invisible to a test script.
        using afterhours::input;
        const bool modDown = input::is_key_down(afterhours::keys::LEFT_SUPER) ||
                             input::is_key_down(afterhours::keys::RIGHT_SUPER) ||
                             input::is_key_down(afterhours::keys::LEFT_CONTROL) ||
                             input::is_key_down(afterhours::keys::RIGHT_CONTROL);
        if (!modDown) return;

        // Cmd on a Mac, Ctrl everywhere else -- and afterhours' e2e combo
        // parser maps "CMD+" onto Ctrl, so a script writing Cmd+0 lands here
        // as Control either way.
        if (input::is_key_pressed(afterhours::keys::EQUAL))
            ui::zoom::step(ui::zoom::kStep);
        if (input::is_key_pressed(afterhours::keys::MINUS))
            ui::zoom::step(-ui::zoom::kStep);
        if (input::is_key_pressed(afterhours::keys::ZERO))
            ui::zoom::reset();
    }
};

}  // namespace ecs

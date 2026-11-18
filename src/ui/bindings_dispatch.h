#pragma once

#include "../ecs/ui_imports.h"
#include "bindings.h"

namespace ui::bindings {

// True on the frame the binding's key is pressed with its modifiers down.
// Extra modifiers do not block it (matching the hand-rolled checks this
// replaces); display-only rows never fire.
inline bool chord_pressed(const Binding& binding) {
    if (binding.key == 0 || !afterhours::input::is_key_pressed(binding.key)) return false;
    const bool super = afterhours::input::is_key_down(afterhours::keys::LEFT_SUPER) ||
                       afterhours::input::is_key_down(afterhours::keys::RIGHT_SUPER);
    const bool ctrl = afterhours::input::is_key_down(afterhours::keys::LEFT_CONTROL) ||
                      afterhours::input::is_key_down(afterhours::keys::RIGHT_CONTROL);
    const bool alt = afterhours::input::is_key_down(afterhours::keys::LEFT_ALT) ||
                     afterhours::input::is_key_down(afterhours::keys::RIGHT_ALT);
    const bool shift = afterhours::input::is_key_down(afterhours::keys::LEFT_SHIFT) ||
                       afterhours::input::is_key_down(afterhours::keys::RIGHT_SHIFT);
    return (!binding.command || super || ctrl) && (!binding.ctrl || ctrl) && (!binding.alt || alt) &&
           (!binding.shift || shift);
}

}  // namespace ui::bindings

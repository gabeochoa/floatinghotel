#pragma once

// Web-like zoom. Adaptive scaling means pixels() = logical px * ui_scale, so
// one knob resizes the whole UI; afterhours clamps it to 0.5..3.0.

#include <afterhours/src/plugins/ui/theme.h>

namespace ui::zoom {

inline constexpr float kStep = 0.1f;
inline constexpr float kMin = 0.5f;
inline constexpr float kMax = 3.0f;

inline float get() {
    return afterhours::ui::imm::ThemeDefaults::get().theme.ui_scale;
}

inline void set(float scale) {
    auto& theme = afterhours::ui::imm::ThemeDefaults::get().theme;
    theme.ui_scale = scale < kMin ? kMin : (scale > kMax ? kMax : scale);
}

inline void step(float delta) { set(get() + delta); }
inline void scale_by(float factor) { set(get() * factor); }
inline void reset() { set(1.0f); }

}  // namespace ui::zoom

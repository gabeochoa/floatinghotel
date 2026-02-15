# Afterhours Framework Issues

Tracking document for issues encountered while building FloatingHotel with the afterhours UI framework.

---

## Open Issues

### `to_ent()` crashes when widget entity is not in the UI mapping

**Severity:** Critical (crash on startup)  
**Status:** Patched locally, needs upstream fix

Introduced in commit `c10c0aa`. The new `children()` text measurement fallback in `compute_size_for_child_expectation` calls `to_ent(widget.id)` to check for `HasLabel`. When `widget.id` isn't in the AutoLayout mapping (e.g. entity 0, created before UI init), `to_ent` dereferences `mapping.end()` — undefined behavior / segfault.

**Fix applied locally** in `vendor/afterhours/src/plugins/autolayout.h`:

```cpp
// Before (crashes):
const Entity &ent = to_ent(widget.id);

// After (safe):
auto it = mapping.find(widget.id);
if (it != mapping.end()) {
    const Entity &ent = it->second;
    if (!ent.is_missing<HasLabel>()) {
        text_size = get_text_size_for_axis(widget, axis);
    }
}
```

---

### `simulate_click` doesn't auto-release mouse button

**Severity:** High (breaks all E2E multi-click tests)  
**Status:** Patched locally, needs upstream fix

`test_input::simulate_click()` sets `left_down = true` and `press_frames = 1`, but never releases `left_down`. The UI system computes `just_pressed = !prev_down && down`, so only the **first** `simulate_click` in a session produces `just_pressed = true`. All subsequent clicks see `prev_down = true` (still held) and never detect a new press transition.

**Symptoms:** After the first `click_button`/`click_text` in an E2E test, all subsequent click commands find the correct element but produce no effect — menus don't open, file selection doesn't change, buttons don't trigger.

**Fix applied locally** in `vendor/afterhours/src/plugins/e2e_testing/input_injector.h` and `test_input.h`:

1. Added `bool auto_release = false` field to `MouseState`
2. `simulate_mouse_press()` sets `auto_release = true`
3. `reset_frame()` auto-releases (`left_down = false`, `just_released = true`) when `auto_release` is true and `press_frames` has expired
4. Explicit `mouse_down` commands (which set `press_frames = 0`) are unaffected since they don't set `auto_release`

```cpp
// In reset_frame(), after the press_frames countdown:
} else if (m.auto_release && m.left_down) {
    m.left_down = false;
    m.just_released = true;
    m.auto_release = false;
}
```

---

## Resolved Issues

### Fixed in afterhours commit `c10c0aa`

- **`percent(1.0f)` resolved to screen width inside absolute-positioned parents** — Fixed by storing absolute position on UIComponent so children compute layout relative to parent.
- **`FlexDirection::Row` children rendered at wrong positions inside absolute ancestors** — Same fix.
- **`children()` sizing didn't account for text measurement** — Fixed by adding text measurement fallback for leaf elements.
- **Inconsistent flow child positioning in absolute-positioned elements** — Same root cause.

### Fixed with `.with_render_layer()`

- **Sibling render order determined by entity creation order** — Resolved by using `.with_render_layer()` on menu bar (5), toolbar (5), status bar (5), and dropdown menus (50+) to control z-ordering explicitly.

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

## Resolved Issues

### Fixed in afterhours commit `c10c0aa`

- **`percent(1.0f)` resolved to screen width inside absolute-positioned parents** — Fixed by storing absolute position on UIComponent so children compute layout relative to parent.
- **`FlexDirection::Row` children rendered at wrong positions inside absolute ancestors** — Same fix.
- **`children()` sizing didn't account for text measurement** — Fixed by adding text measurement fallback for leaf elements.
- **Inconsistent flow child positioning in absolute-positioned elements** — Same root cause.

### Fixed with `.with_render_layer()`

- **Sibling render order determined by entity creation order** — Resolved by using `.with_render_layer()` on menu bar (5), toolbar (5), status bar (5), and dropdown menus (50+) to control z-ordering explicitly.

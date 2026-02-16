# Afterhours Framework Gaps

Issues encountered while trying to match a precise HTML/CSS mockup in the afterhours UI framework. Each section describes what we needed, what the framework provided at the time, the resolution status, and any remaining work.

---

## RESOLVED

### 1. Custom Hover Background Color — RESOLVED (bff4609)

**Was:** Hover force-overrode background to `Theme::Usage::Background`. No per-component control.

**Now:** `with_custom_hover_bg(Color)` on `ComponentConfig`. Stores optional hover color on `HasColor`. Rendering uses it instead of the global theme color.

**Integration:** Applied to file rows, untracked rows, and commit rows in `sidebar_system.h`.

---

### 3. Text Overflow / Ellipsis — RESOLVED (120a9ed) — **BUG: hangs with expand() sizing**

**Was:** Text clipped silently or showed debug indicator. Manual truncation with `substr()` was fragile.

**Now:** `with_text_overflow(TextOverflow::Ellipsis)` on `ComponentConfig`. Renderer binary-searches for longest fitting prefix and appends "...".

**Bug found:** Using `with_text_overflow(Ellipsis)` on elements sized with `expand()` (or `children()`) causes an infinite hang on launch. The binary search in the truncation code likely runs against a 0-width container before layout resolves, creating an infinite loop. Only safe to use on elements with fixed pixel widths.

**Integration:** Removed from file name labels (which use `expand()`). Still used for commit subject buttons — **NOTE: this was also removed during debugging**. The manual `subjectText.substr()` truncation was removed from commit rows but text overflow is not applied as a replacement. Commit subjects currently clip at container boundary.

---

### 4. Flex Gap — RESOLVED (37fe6f4)

**Was:** No CSS-like `gap`. Every child needed manual right margin.

**Now:** `with_gap(Size)` on `ComponentConfig`. Layout engine adds spacing between adjacent flow children.

**Integration:** Applied to toolbar button row. Removed per-button `with_margin(Margin{.right = ...})`.

---

### 6. Per-Side Border — RESOLVED (9eb0796)

**Was:** `with_border()` drew all four sides. Bottom-only borders required extra 1px divs.

**Now:** `with_border_top()` / `with_border_right()` / `with_border_bottom()` / `with_border_left()` methods. `Border` struct supports per-side `BorderSide` configuration.

**Integration:** Applied to toolbar (bottom border) and diff file header (bottom border). Removed separate 1px border divs.

**Note:** Had to fix a `DrawRectangle` → `draw_rectangle(RectangleType{...})` call in `rendering.h` for the Metal rendering path (raylib-ism in the immediate renderer).

---

### 7. Default Transparent Background — RESOLVED (778f786)

**Was:** Child elements without explicit background got theme defaults, causing spurious colored boxes.

**Now:** Elements without a color usage default to `colors::transparent()` instead of pulling from the theme.

**Integration:** Existing `with_transparent_bg()` calls are now redundant but left for clarity. No code changes needed — the framework fix applies globally.

---

### 8. Absolute-Positioned Children Inherit Parent Position — RESOLVED (1cb50a3)

**Was:** Children of absolute-positioned elements rendered at screen (0,0). Status bar was a brittle hack of sibling absolute divs.

**Now:** Test added verifying absolute parent children inherit position. Status bar could be refactored to use normal parent-child flow.

**Integration:** Not yet applied to status bar — refactoring deferred. The fix is available when we next touch the status bar.

---

### 10. Letter Spacing — RESOLVED (bff4609)

**Was:** No `letter-spacing` equivalent. Section headers looked slightly cramped.

**Now:** `with_letter_spacing(float)` on `ComponentConfig`. Adjusts character spacing during text measurement and rendering.

**Integration:** Applied to section headers and commit log header in `sidebar_system.h` with `0.5f` spacing.

---

### 9. Cursor Change on Hover — RESOLVED (27b535e)

**Was:** No API to change mouse cursor when hovering over interactive elements.

**Now:** `with_cursor(CursorType::Pointer)` / `CursorType::ResizeH` / `CursorType::ResizeV` / `CursorType::Text` on `ComponentConfig`. Rendering loop checks `HasCursor` on hot entities and calls `set_mouse_cursor()`.

**Integration:** Applied to file rows, untracked rows, commit rows (Pointer), sidebar vertical divider (ResizeH), sidebar horizontal divider (ResizeV), and toolbar buttons (Pointer).

**Note:** `DrawRectangle` → `draw_rectangle(RectangleType{...})` fix still needed in the Metal path for per-side borders (same fix as Gap 6). Also, incremental builds may crash with `std::length_error: vector` after pulling this commit — a `make clean` is required due to component ID changes in headers.

---

## STILL OPEN

### 2. No Font Weight Support

**Problem:** `ComponentConfig` has `with_font()` and `with_font_size()` but no `with_font_weight()`. The mockup uses `font-weight: 600` on diff file headers and status letters. The only way to approximate bold text is to load a separate bold font file and switch font names per-component, which is cumbersome.

**Impact:** Diff file headers and status badge letters should be semi-bold but render at normal weight.

**Suggested fix:** Add a `FontWeight` enum and a `with_font_weight(FontWeight)` method. The text rendering path would select the appropriate font variant at render time.

---

### 5. No Rich Text / Multi-Color Text in a Single Label

**Problem:** Each `div` or `button` can only have one text color. To show a filename in white and its directory path in gray on the same row, you need two separate child `div` elements.

**Impact:** File rows compose `"filename  dir"` as a single string but can't color them differently. Status letters (M, A, D, U) should be individually colored but are the same color as the filename. Diff lines that need sub-line syntax highlighting are impossible without multiple elements.

**Workaround:** Bake status letter and filename into a single label string (e.g. `"M  README.md"`). Colored status letters are sacrificed.

**Suggested fix:** Support a `StyledText` API:
```cpp
.with_styled_label({
  {"M ",        theme::STATUS_MODIFIED},
  {"theme.h ",  theme::TEXT_PRIMARY},
  {"src/ui",    theme::TEXT_SECONDARY}
})
```

---

### 11. Row Flex Layout Broken with expand() Children

**Problem:** When a `button` or `div` with `FlexDirection::Row` contains children, any child sized with `expand()` consumes the full parent width instead of the remaining width after fixed-size siblings. This causes siblings to wrap to the next line or be pushed off-screen.

**Impact:** Cannot create a row like `[status_letter(16px) | filename(expand)]` — the filename fills 100% and the status letter wraps below. Similarly, commit rows cannot inline badges after an expanding subject label.

**Workaround:** Bake all content into a single label string on the parent element, avoiding child elements entirely. For commit rows, badges/hash are rendered as a separate second-line `div` below the subject.

**Suggested fix:** The autolayout engine should calculate `expand()` as `parent_content_width - sum(fixed_sibling_widths)` in Row flex, matching CSS `flex: 1` behavior.

---

### 12. Adaptive Scaling Mode (Web-Like Font Sizing)

**Not a gap** — documenting a configuration choice.

**Was:** Using `ScalingMode::Proportional` with `h720()` for font sizes. Fonts scaled proportionally with screen resolution (13px at 720p → 26px at 1440p). This made text appear larger on high-DPI displays.

**Now:** Switched to `ScalingMode::Adaptive` with `pixels()` for font sizes. Fonts behave like CSS `px` — they stay at their declared size regardless of resolution. Layout dimensions still use `h720()`/`w1280()` where proportional scaling is desired.

**Font size mapping (matches HTML mockup):**
| Component | CSS px | Theme constant |
|-----------|--------|----------------|
| Section headers | 11px | `FONT_CAPTION` (11.0f) |
| Toolbar buttons | 12px | `FONT_META` (12.0f) |
| Hunk headers | 12px | `FONT_META` (12.0f) |
| Status bar | 12px | `FONT_META` (12.0f) |
| Menu bar, file rows, commit rows | 13px | `FONT_CHROME` (13.0f) |
| Diff code | 13px | `FONT_CODE` (13.0f) |
| Diff file header | 14px | `FONT_HEADING` (14.0f) |
| Commit detail subject | 18px | `FONT_HERO` (18.0f) |

---

## Summary

| # | Gap | Status | Commit |
|---|-----|--------|--------|
| 1 | Custom hover background | **RESOLVED** | a0c2b03 |
| 2 | Font weight | **OPEN** | — |
| 3 | Text overflow ellipsis | **RESOLVED** | 120a9ed |
| 4 | Flex gap | **RESOLVED** | 37fe6f4 |
| 5 | Rich/multi-color text | **OPEN** | — |
| 6 | Per-side border | **RESOLVED** | 9eb0796 |
| 7 | Default transparent bg | **RESOLVED** | 778f786 |
| 8 | Absolute child positioning | **RESOLVED** | 1cb50a3 |
| 9 | Cursor changes | **RESOLVED** | 27b535e |
| 10 | Letter spacing | **RESOLVED** | bff4609 |
| 11 | Row flex with expand() | **OPEN** | — |
| 12 | Adaptive scaling mode | **INFO** | — |

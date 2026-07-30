# afterhours UI — annoyances / footguns / wishlist

Running notes while building floatinghotel's UI on the afterhours immediate-mode UI
library (sokol/Metal backend). Intended to share with the afterhours maintainers.
Each item: what bit us, and what would make it better.

## Text rendering

1. **Labels don't word-wrap.** `with_word_wrap(true)` only affects multiline
   *text-area* widgets, not plain `div`/label text. A long single-line label
   (e.g. a commit-message body) just overflows its container off-screen. There's
   no way to make a normal label wrap.
   *Wish:* a `with_word_wrap`/multiline mode for plain labels bounded by their
   `percent()`/`pixels()` width.

2. **Font atlas is ASCII-only (Basic Latin).** Any non-ASCII glyph — arrows
   (`←`, `▾`), checkmarks (`✓`), box-drawing, icon glyphs — renders **blank**.
   This makes it impossible to match icon-heavy mocks without shipping an icon
   font/texture. We had to fall back to ASCII (`<- Back`, "More" instead of
   "More ▾", text buttons instead of icon buttons).
   *Wish:* document the atlas coverage prominently; support an icon font or
   supplemental glyph ranges; or a small built-in icon set.

3. **No measured text width for layout heuristics.** Menu-header widths are
   computed from a hardcoded `charW ≈ 10px/char` estimate, which is wrong when
   the font size changes (had to hand-tune when tiers changed). `measure_text`
   exists but isn't wired into common layout paths.
   *Wish:* a layout size unit that resolves to measured text width.

4. **Ellipsis vs. clip vs. wrap is under-documented.** `TextOverflow` enum
   (`Clip`/`Ellipsis`) exists but the default is `Clip` and the interaction with
   `NoWrap`/flex is easy to get wrong.

## Layout / sizing

5. **`expand()` is buggy in Row flex.** There's a known Row-flex `expand()` bug
   (referenced in the repo's own `docs/afterhours-gaps.md`); you can't reliably
   use `expand()` to fill remaining space, so we compute explicit pixel heights
   everywhere (e.g. scroll-panel viewport = section height − hardcoded header
   reserve). Fragile against padding/rounding changes.

6. **`percent()` width doesn't resolve reliably in nested containers.** The
   sidebar threads an explicit `pixels(sidebarPixelWidth_)` down to child
   sections to work around percent() resolving wrong inside nested divs. Adds a
   lot of manual width plumbing.

7. **Roundness is a fraction, not pixels.** `with_roundness(f)` is a fraction of
   half the min dimension, so getting a specific "8px radius card" requires
   back-solving per element size, and the same fraction looks very different on a
   tall card vs a short button.
   *Wish:* a pixel-based radius option.

## ECS / systems

8. **`find_singleton(force_merge=true)` during a system tick can invalidate
   iteration → heap-use-after-free.** `SystemManager::tick` iterated the entity
   vector with a range-`for`; a system's `for_each` calling a `force_merge`
   query `push_back`s into that same vector and reallocates it, so the loop then
   walks freed memory. Flaky/heap-dependent (ASan-confirmed). We fixed it by
   switching `tick`/`fixed_tick`/`render` to index-based iteration.
   *Wish:* make the built-in iteration realloc-safe, or make `force_merge`
   defer the merge to a safe point.

## Backend / headless

9. **No true headless (windowless) rendering for sokol/Metal** (was
   `@notimplemented`). We added it: create our own `MTLCreateSystemDefaultDevice`,
   `sg_setup` with no swapchain, render into an offscreen texture. But `sapp_*`
   calls (`sapp_dpi_scale/width/height`) are scattered through the draw/measure
   paths and assume a live sokol_app; had to add a `metal_detail` shim to feed
   headless values.
   *Wish:* first-class headless support; route platform queries through one
   swappable seam instead of direct `sapp_*` calls.

10. **Offscreen render-texture readback needs manual GPU sync.** Non-MSAA
    Private Metal render targets return garbage from `getBytes`; had to blit to a
    Shared texture + `waitUntilCompleted`. Not obvious.

11. **`window_manager` forward-declares `sapp_*` to stay decoupled**, which meant
    the headless resize path couldn't reach `graphics::` helpers without adding a
    `graphics.h` include. Minor, but the decoupling fought us.

## E2E / testing

12. **Mouse-wheel injection is consume-once and order-sensitive.** The e2e
    `scroll_wheel` sets an injected wheel that `get_mouse_wheel_move_v()`
    *consumes* on first read; whichever system reads first wins, and it's cleared
    per frame — so driving a specific scroll view headlessly is unreliable. The
    real app reads a live (re-readable) wheel, so headless behavior diverges from
    real.
    *Wish:* non-consuming wheel reads in test mode, or per-target wheel routing.

13. **Scroll `content_size` gating.** `HasScrollView` only enables scroll when
    `content_size.y > viewport_size.y`, and `content_size` is summed in
    `FixScrollViewPositions` from resolved child heights — if heights aren't
    resolved on a frame, scroll silently disables. Hard to debug.

## Ergonomics / wishlist

14. Many call sites bypass the `FontSize` tier enum with raw `h720(px)` /
    `pixels(px)`; nothing discourages it, so typography drifts. A lint or a
    "no raw font sizes" mode would help keep to a scale.
15. `with_border` / `with_border_bottom` are handy, but there's no single
    "card" preset (bg + border + radius + padding) so every card re-specifies it.
16. Absolute-positioned children need manual `with_render_layer` to stack
    correctly (e.g. graph dots over lines); easy to forget.

# Afterhours Gaps & Missing Primitives

Tracking afterhours features/bugs that floatinghotel needs but are not yet implemented upstream. Each entry includes what's missing, what floatinghotel needs it for, and the workaround.

**Policy:** Never edit `vendor/afterhours/` directly. Build workarounds in `src/ui/` and document gaps here so the afterhours maintainer can address them upstream.

**Reviewed against Afterhours `5383fc6c1d1710722f8d3bdc727c0e6491815765`**
(upstream `main`, 2026-11-25), bumped from `e9d5f67` on 2026-11-26. The
detailed sections below are the evidence log that produced many of the
upstream fixes; this summary is the current, authoritative status.

Adopted with this bump, deleting the app workaround rather than keeping a
parallel implementation: upstream macOS window sizing and live-resize drawing,
first-frame early draw, modal centering, `EntityHandle` focus restore,
upstream scroll-input/scrollbar gating, direct `text_area` (including its
placeholder and Adaptive row sizing), direct `virtual_list`, pending-render
clearing in `BeginUIContextManager`, absent-texture clearing, `FocusProxy`,
widget-owned directional input, `shrink_font_atlas`, polyline chevrons,
`scroll_to_entity`, public RGBA texture upload, the built-in E2E
`hold_key`/`release_key` commands (all scripts now use key names), the E2E
render checkpoint in the app's first-match click handler, scoped
`assert_ui_text`, and `expect_unread_key` for intentionally ignored presses.

Still deliberately app-owned below because the upstream shape does not yet
match FloatingHotel's semantics: the future-based executor, FSEvents watcher,
exact byte-accounted caches, semantic focus/tree navigation, cross-row diff
selection, native menus, toast presentation, and the JSON layout dump. The
specific missing flexibility is listed in **Remaining gaps after the
`5383fc6` bump** immediately below; older “OPEN” wording later in this file is
historical unless that summary carries it forward.

Deliberately not adopted, with reasons in the entries below:
`with_styled_label` (child divs already do it better here),
`hsplit_pane`/`vsplit_pane` (panels are absolutely positioned, no container to
wrap), `animation::set_instant` (the animation plugin is not registered — the
toast plugin does not use it), `with_font_weight` (needs bold font files).

## Remaining gaps after the `5383fc6` bump

- **Deferred native presentation.** Upstream draws the first frame early, but
  has no start-hidden/present-when-ready lifecycle, hidden-frame driver, or
  activation-policy handoff. FloatingHotel still owns the AppKit swizzles and
  a 60 Hz startup draw timer in `src/sokol_impl.mm`.
- **Future-shaped jobs.** `jobs::Pool` delivers results only through an
  owner-thread `drain()` callback. It has no pollable future, exception
  channel, caller-supplied rejected value, non-cancellable jobs, or
  shutdown-drains-accepted-work semantics. Those are load-bearing in
  `src/util/async_task.h`, so the app executor stays.
- **Scalable recursive file watching.** Upstream `file_watcher::Watcher` is
  kqueue-backed and opens an FD for every directory and regular file. That is
  not a safe replacement for one FSEvents stream over a large repository plus
  its Git common dir. An FSEvents (or equivalent coalesced recursive) backend
  behind the same API would let `src/platform/file_watcher.h` go.
- **Cache accounting.** Upstream `ByteCache` costs only the value. FloatingHotel
  also charges key capacity, index/list overhead, and evicted-but-still-referenced
  shared rows. A cost callback receiving key+value (and an accounted-bytes hook
  for externally retained entries) is needed before `src/util/byte_cache.h` and
  `src/util/shared_byte_cache.h` can be deleted without weakening budgets.
- **Virtualized tree widget.** `TreeViewConfig::render_row` landed, but
  `tree_view` still recursively builds every expanded row and owns selection /
  expansion internally. It has no virtual row window, external semantic focus
  identity, keyboard/type-to-select controller, or reveal contract compatible
  with FloatingHotel's flattened, virtualized file trees.
- **Menu flexibility.** Framework menus now handle placement, scrolling, marks,
  dismissal and focus return, but items have no destructive styling, per-item
  debug names, custom row renderer, or integration with FloatingHotel's shared
  dismissal stack. The app menu bar and context-menu renderer therefore stay.
- **Thin-divider drag capture.** `divider()` now expands a sub-12px hit rect,
  but replacing the commit splitter's 16px band with a true 1px divider failed
  `tests/sidebar_splitter.py` at the top clamp: after moving the held pointer to
  y=0, moving back to y=20 no longer moved it. Pointer capture for an active
  divider drag needs to survive leaving the expanded hit rect (and crossing the
  menu bar) before the app composition can be deleted.
- **Storage-codec legacy input.** `storage::Codec` only decodes its own
  envelope. FloatingHotel's review/settings files are versioned JSON documents
  with identity fields and copy/relink validation; a legacy-unversioned decode
  hook would be needed for a no-flag-day migration.
- **Still unverified or missing upstream:** a visibility predicate distinct
  from text existence, joined styled-span text assertions, consistent
  `skip_grid_snap` flow extents, a safe texture-retirement policy distinct from
  component clearing, Sokol honoring `target_fps`, color emoji, and a shared
  dismissal stack across menus/popovers/modals. The detailed entries below keep
  the reproductions for these.

---

## Missing Primitives

### 1. Draggable Divider — RESOLVED upstream (48f808d), adopted
`imm::divider(ctx, mk(...), Axis)` is truthy on the frames it moved, and
`.as<float>()` is that frame's travel in `rect()` space.

`render_sidebar_divider` uses it. The hand-rolled version polled the raw
backend mouse position and undid letterboxing itself
(`mouseX * 1280.0f / sw`), and jumped whenever you grabbed the bar off centre,
because it set the width from the cursor position instead of accumulating a
delta. Covered by `flow_sidebar_resize.e2e`.

### 2. Split Pane — RESOLVED upstream (48f808d), not needed
`imm::hsplit_pane` / `vsplit_pane` wrap a divider around two regions, taking a
`float&` ratio the drag updates in place. floatinghotel positions its panels
absolutely from `LayoutComponent` rather than nesting them, so there is no
container to hand to a split pane. The divider alone is the part it needed.

### 3. Tree view exists; custom rows and large trees need an extension

`ui::imm::tree_view`, `TreeNode<T>`, `TreeViewConfig<T>`, and
`HasTreeViewState` exist in `vendor/afterhours/src/plugins/ui/tree_view.h` at
b385dc9. The widget already nests nodes and tracks expansion and selection by
caller-supplied string IDs. The earlier claim that no tree widget exists was
stale, and `src/ui/tree_view.h` is not an app file.

The app builds its trees in `src/ecs/sidebar_system.h`, with flattening helpers
in `src/util/file_tree.h`. Rows contain status, path, counts, and review state.
At `5383fc6`, `TreeViewConfig::render_row` supplies the custom row callback this
entry originally asked for. The widget still recursively builds every expanded
node and owns selection/expansion internally; it has no virtual row model,
external semantic focus identity, keyboard/type-to-select controller, or reveal
contract. Those are the remaining blockers, not row rendering.

Status on 2026-11-26: custom rows are resolved upstream. Virtualization and
the controller/focus contract remain open, so FloatingHotel keeps its flattened
rows over `virtual_list`.

### 4. Dropdown menu exists; app adoption remains

`ui::imm::dropdown_menu` and `MenuItem` exist in
`vendor/afterhours/src/plugins/ui/menu.h` at b385dc9. Items already support labels,
shortcut hints, separators, and disabled state. The widget returns a selected
index, so the host can dispatch its own command without framework callbacks.

The app still renders menu bars in `src/ecs/menu_bar_system.h` and context menus
in `src/ui/context_menu_render.h`. Basic dropdown support is therefore adoption
work, not a missing framework feature. The native menu adapter is covered under
`macOS menu integration` below. Scoped command routing and focus return are
separate requests in the 2026-09-12 audit at the end of this document.

Status: source-verified availability. No upstream menu rendering test was run
in this audit, and the older last-item click report is not marked fixed.

### 5. Context Menu — RESOLVED (176ea8f upstream + app), adopted
`ctx.is_right_click(id)` answers "a secondary click finished over this element
or something inside it". The target needs a click or drag listener, since that
is what hit-testing resolves against; asking about a plain `div` warns rather
than silently never firing.

`src/ui/context_menu.{h,cpp}` had sat with no callers since there was no
right-click to wire it to. It now renders (`render_context_menu`, called last
from `MenuBarSystem` so it lands above the menu dropdowns) and file rows open
it with Stage/Unstage plus Copy Path. Covered by
`flow_context_menu_file.e2e`, which clicks the item and checks the file was
actually staged.

Still to do: menus on commits (copy hash, cherry-pick, revert) and branches.
No Discard yet — there is no `discard_file` git command, and a destructive one
wants a confirmation step.

### 6. Anchored popup exists; app adoption remains

`ui::imm::popover` in `vendor/afterhours/src/plugins/ui/menu.h` accepts an anchor
rectangle, an open flag, a preferred placement, and caller-drawn content.
`ui::overlay::place` in `ui/overlay.h` flips and clamps against the screen bounds.
The old request for basic anchored popup placement has shipped at b385dc9.

The app's manually placed menus remain in `src/ecs/menu_bar_system.h` and
`src/ui/context_menu_render.h`. Adopting the existing popup API requires checking
nested-scroll coordinates, zoom, and focus behavior against those menus.
Availability was verified from source on 2026-09-12. Those integration checks
were not run, so this entry does not claim the app workaround can yet be removed.

---

## Styling & Layout Gaps

### Text Overflow / Ellipsis — RESOLVED (120a9ed)

`with_text_overflow(TextOverflow::Ellipsis)` on `ComponentConfig`. The renderer
binary-searches for the longest fitting prefix and appends "...".

The hang this entry reported with `expand()`/`children()` sizing is fixed
upstream, so the "fixed pixel widths only" restriction no longer applies.

This resolution does not cover colored spans. The styled-label renderer still
bypasses truncation, as recorded under
[Styled labels bypass ellipsis truncation](#styled-labels-bypass-ellipsis-truncation).

---

### Div backgrounds render opaque — no alpha blend for overlays — RESOLVED, adopted

A `div` background alpha-blends now, so `with_custom_background(Color{r,g,b,a})`
with a low `a` tints what is underneath instead of hiding it. `with_opacity`
also scales a colour's existing alpha rather than replacing it.

The diff drag-to-select highlight (`src/ui/diff_renderer.h`, `diff_sel_hl`) is
a plain translucent box over the already-rendered line — the opaque-box-plus-
re-drawn-substring workaround this entry described is gone.

---

### Styled labels are available; per-column layout remains separate

`with_styled_label({{"M ", STATUS_MODIFIED}, {"theme.h ", TEXT_PRIMARY}, ...})`
is on `ComponentConfig` in `component_config.h`.

`render_file_row_impl` keeps separate child divs for the status glyph, filename,
and directory. Each column has its own width and ellipsis. A single styled label
would lose that separation. The original workaround below no longer describes
these file rows.

Styled labels are now used for diff paths and hunk captions. That adoption
exposed the styled-ellipsis bug linked above. Availability of both APIs does
not mean their combination works.

The original “no rich text” request is resolved by `with_styled_label`; only
the per-column layout choice above remains app-side.

---

### `with_font_weight` — font registration required; bold UI font adopted locally

`with_font_weight` is on `ComponentConfig`. It looks up a font registered as
`"<font>@bold"` and falls back to the base font when there is none, which is
why this read as "no font weight support"; since `90f8ae8` that fallback warns
once instead of being silent.

The review-focus UI now includes `resources/fonts/Roboto-Bold.ttf` and its
Apache license from googlefonts/roboto-2. `preload.cpp` registers it as
`ui-bold`, and the commit heading selects that font explicitly. The normal
build copies resources so the font is available in the output directory.
No framework changes were needed. Automatic `with_font_weight` selection
would still require the corresponding `@bold` registration; a bold monospace
font has not been added.

---

### Row Flex Layout Broken with expand() Children — RESOLVED (0c67090), adopted

`expand()` in a Row now takes the remaining width after fixed-size siblings,
which is what CSS `flex: 1` does and what this entry asked for. Verified at
several sidebar widths: `[status(20px) | filename(expand) | dir(90px)]` keeps
all three on one line and gives the name the slack.

The sidebar file row was computing `nameW = totalW - dirW - GAP*2 - STATUS_W`
in app code to work around it. That arithmetic is gone; only the dir column
carries a width now.

---

### Custom Colors Bypass Disabled Dimming — RESOLVED

`resolve_background_color()` now routes a custom colour through
`theme.disabled_variant()` when `disabled` is set, which mixes toward the
background by `Theme::disabled_opacity` (0.3) and scales alpha to match. So
`with_disabled(true)` changes the appearance of a `with_custom_background()`
element, not just its hit-testing.

The presets still pick their own disabled bg/text rather than leaning on this,
which is deliberate — `theme::DISABLED_BG`/`DISABLED_TEXT` are chosen colours,
not a generic 30% mix.

---

### `with_font_tier()` is deprecated — use `with_font_size(FontSize::Small)`

The `h720()`-only complaint stands, but the method itself is now marked
`[[deprecated]]` in favour of `with_font_size(FontSize::...)`, which is what
floatinghotel already uses everywhere. Nothing to do.

---

## Resolved Styling Gaps

| # | Gap | Commit |
|---|-----|--------|
| 1 | Custom hover background | a0c2b03 |
| 3 | Text overflow ellipsis | 120a9ed |
| 4 | Flex gap | 37fe6f4 |
| 6 | Per-side border | 9eb0796 |
| 7 | Default transparent bg | 778f786 |
| 8 | Absolute child positioning | 1cb50a3 |
| 9 | Cursor changes | 27b535e |
| 10 | Letter spacing | bff4609 |
| 12 | Adaptive scaling mode | SUPERSEDED |

---

## App bugs found while adopting the above

### The last item of a menu dropdown does not respond to clicks

Clicking it closes the dropdown (so it looks like it worked) but never runs the
item's action. Reproduced with `View > Reset Zoom`: the identical click
sequence ending on `Zoom Out` (second to last) changes `ui_scale`, and ending
on `Reset Zoom` leaves it untouched. `MenuBarSystem` computes each item's hover
rect by accumulating `itemY`, and the same `itemY` positions the button, so the
two ought to agree -- they do not for the final entry.

It went unnoticed because the tests covering those items assert on a toast, and
toasts outlive the click: `flow_stub_toast_view_menu` clicked three items in a
row that all toast the same "not yet implemented" string, so the third
assertion matched the first item's toast. `flow_stub_toast_edit_menu` has the
same shape on `Find...`, which is the last Edit item -- suspect that one too.

## Known Vendor Bugs

### tab_container() position bug
- **Issue:** Tab strip renders at screen-absolute position, ignoring parent container bounds.
- **Impact:** Cannot use `tab_container()` for multi-repo tabs.
- **Workaround:** Build manual tab buttons in a row using `div()` + `button()`.

### toggle_switch() layout issue
- **Issue:** Creates sibling entities that consume extra layout space.
- **Impact:** Toggle switches misalign adjacent elements.
- **Workaround:** Use `with_no_wrap()` on parent, increase container height.

### text_input() InputAction enum values — RESOLVED upstream
Text editing now resolves host action names when present and falls back to the
library's own chords when they are absent. FloatingHotel keeps its named
actions (including the new `TextDeleteLineBack`/`TextDeleteLineForward`) so
the full default keymap binds, but a missing host enum no longer makes the
widget unusable.

### Clipboard shortcuts not wired in text_input — RESOLVED, adopted
- afterhours implements copy/cut/paste/undo/redo/word-motion/shift-selection and
  binds them in `ui::default_keymap<InputAction>()`, matched to the app's enum
  **by name**. A name the enum does not have compiles the feature out silently,
  which is how the commit box went without them for so long; the library warns
  about the missing names now.
- `src/input_mapping.h` carries all eleven, and `src/preload.cpp` takes
  `default_keymap` instead of hand-listing four keys.

---

### Draw capture on the sokol/Metal backend — RESOLVED upstream
`backends/sokol/drawing_helpers.h` now records text, rectangles, lines,
circles, quads and textures through `capture::record`, so the draw assertions
have the same backend coverage as raylib/none. No app workaround remains.

### `with_placeholder` in `text_area` — RESOLVED upstream, adopted
`text_area` now renders its placeholder in the empty first row. The commit
box uses it for its branch hint (deleting the label stacked above the field),
and the ACP prompt/MCP editors use it too.

### text_area lines overflow their own field — RESOLVED upstream, adopted
Generated `text_area_line` rows now use `percent(1.f)` width and resolve their
height in the field's scaling mode. FloatingHotel deleted `src/ui/text_area.h`
(the adapter that rewrote those widths after construction); the commit,
comment, basket-edit, ACP prompt and MCP fields call
`afterhours::text_input::text_area` directly. `tests/check_text_area_layout.sh`
passes at 100% and 140%.

### Sokol font atlas growth — upstream tools adopted; shrink policy stays app-owned
The atlas now expands when full instead of dropping glyphs, reports packing
failures, and exposes `afterhours::shrink_font_atlas()` /
`font_atlas_bytes()`. `ecs::ZoomSystem` calls the supported shrink hook on a
zoom change instead of reaching into `metal_detail::g_fons_ctx` with
`fonsResetAtlas`. Upstream still does not choose when to shrink; that frame
boundary policy remains FloatingHotel's one-line zoom hook. Regression:
`tests/e2e_scripts/flow_pinch_sweep.e2e`.

### Prepared upstream branches — superseded by the `5383fc6` bump
The October prepared-branch work has landed in the revision above: E2E test
support, text fallback, public `load_texture_from_rgba`, native window sizing,
and atlas expansion/shrink hooks are all upstream and adopted where noted.
Colour emoji and a full migration of FloatingHotel's semantic focus restoration
to framework tokens remain open. The old branch-specific build warnings no
longer describe `main`; the app builds on `5383fc6`.

### Font codepoint coverage on Sokol — RESOLVED upstream
The Sokol font helper now parses each loaded face's cmap into
`font_coverage::Ranges`, exposes `font_has_glyph`, and routes drawing through
`drawable_text`. FloatingHotel no longer needs to guess coverage before using a
glyph; any remaining ASCII marks are product choices, not a backend limit.

---

### sokol backend does not compile after b3f8cef (letterbox in backend.h) — RESOLVED upstream (b385dc9), adopted
Fixed as suggested, plus a `sokol_include_order_test` that includes
`window_manager.h` so the ordering cannot silently regress again. Bumped to
b385dc9 on 2026-09-10: builds clean, unit 5/5, E2E 98/98.
- **Upstream range:** 0c67090..ac1062f (28 commits, tried 2026-09-10). Every
  sokol/Metal build fails; wm is raylib so upstream never sees it.
- **Issue:** `backends/sokol/backend.h:753` `MetalPlatformAPI::get_mouse_position`
  calls `window_manager::window_to_content`, but `backend.h` is pulled in by
  `graphics.h`, which `window_manager.h` includes *before* it declares the
  struct. Error: `use of undeclared identifier 'window_manager'`.
- **Suggested fix:** do what the raylib branch already does. Leave the backend
  at the DPI divide (window space) and apply the letterbox in
  `plugins/input_system.h`'s Metal branch, which can see `window_manager`:
  ```cpp
  // backends/sokol/backend.h — get_mouse_position
  float dpi = sapp_dpi_scale();
  return {s.mouse_x / dpi, s.mouse_y / dpi};

  // plugins/input_system.h — Metal branch
  static MousePosition letterboxed_mouse_position() {
      auto p = graphics::MetalPlatformAPI::get_mouse_position();
      const Vector2Type mapped = window_manager::window_to_content(
          Vector2Type{p.x, p.y},
          graphics::MetalPlatformAPI::get_screen_width(),
          graphics::MetalPlatformAPI::get_screen_height());
      return {mapped.x, mapped.y};
  }
  static MousePosition get_mouse_position() {
  #ifdef AFTER_HOURS_ENABLE_E2E_TESTING
      return testing::test_input::get_mouse_position<MousePosition>(
          []() { return letterboxed_mouse_position(); });
  #else
      return letterboxed_mouse_position();
  #endif
  }
  ```
  Verified here: builds, 98/98 e2e, and the e2e mouse injection still works.

### Flex solver budgets raw child sizes while placement uses snapped ones (since 7a56f60) — RESOLVED upstream (7b84bd0), adopted
Upstream found a third site the note below missed: the `total_main_size`
loop that `remaining_space` derives from, which FlexEnd and Center offset by.
All 45 remaining `Layout overflow` warnings after the bump are the known
`text_area_line` noise.
- **Upstream range:** same bump. 7a56f60 "Place expand children by the width
  they end up with" made placement read `snapped_extent`, but `_total_child`,
  `_max_child` (autolayout.h ~1040) and the justify-content pass (~1412) still
  sum `child.computed + computed_margin`.
- **Impact here:** a row of eight `w1280()` toolbar buttons each round up a
  pixel or two; the `expand()` spacer is handed the pre-snap slack and the
  branch button ends 15px past the toolbar, visibly clipped when the sidebar
  is hidden. The commit dialog's `FlexEnd` button row starts 11px too far
  right so `stage_all_btn` overflows `dialog_buttons`. A/B against 0c67090 on
  `flow_t5_push_toolbar|baseline_all_flows|flow_zoom`: toolbar overflow
  0 → 25 warnings, stage_all 0 → 19. With the fix below both return to 0 and
  the full suite is back to the pre-bump 45 (all `text_area_line`).
- **Suggested fix:** sum what placement will use, in all three places:
  ```cpp
  const auto _total_child = [this, &layout_children](Axis axis) {
    float sum = 0.f;
    for (UIComponent *child : layout_children)
      sum += snapped_extent(*child, axis) + child->computed_margin[axis];
    return sum;
  };
  const auto _max_child = [this, &layout_children](Axis axis) {
    float max_val = 0.f;
    for (UIComponent *child : layout_children)
      max_val = fmaxf(max_val, snapped_extent(*child, axis) +
                                   child->computed_margin[axis]);
    return max_val;
  };
  // justify-content pass
  float cx = snapped_extent(child, Axis::X) + child.computed_margin[Axis::X];
  float cy = snapped_extent(child, Axis::Y) + child.computed_margin[Axis::Y];
  ```
- **Regression test** for `tests/sizing_repro_test.cpp`; overflows by 16px
  without the fix, passes with it (62/62, autolayout 358/358, grid 25/25,
  full `make test` green):
  ```cpp
  TEST(snapped_children_stay_inside_expand_and_flex_end_rows) {
    // Each width sits 2px under a grid line so nearest-snapping rounds all
    // eight up: 16px the raw sum does not know about. Whole-unit margin so the
    // position accumulator cannot drift on its own.
    const float widths[] = {86.f, 102.f, 122.f, 78.f, 58.f, 58.f, 66.f, 94.f};
    for (bool with_spacer : {true, false}) {
      ImmTestHarness h;
      auto cfg = ComponentConfig{}
                     .with_size(ComponentSize{pixels(1256.f), pixels(30.f)})
                     .with_debug_name("row");
      if (!with_spacer)
        cfg = cfg.with_justify_content(JustifyContent::FlexEnd);
      auto row = hstack(h.context(), mk(h.root(), 0), cfg);
      int id = 0;
      for (int i = 0; i < 8; i++) {
        if (with_spacer && i == 7)
          div(h.context(), mk(row.ent(), id++),
              ComponentConfig{}
                  .with_size(ComponentSize{expand(), h720(1.f)})
                  .with_debug_name("spacer"));
        div(h.context(), mk(row.ent(), id++),
            ComponentConfig{}
                .with_size(ComponentSize{w1280(widths[i]), h720(28.f)})
                .with_margin(Margin{.right = w1280(4.f)})
                .with_debug_name("btn_" + std::to_string(i)));
      }
      h.layout_only(true, {1280, 720});
      UIComponent *r = h.find("row");
      UIComponent *last = h.find("btn_7");
      CHECK(r != nullptr && last != nullptr);
      if (!r || !last) continue;
      const float row_end = r->rect().x + r->rect().width;
      const float last_end = last->rect().x + last->rect().width;
      CHECK(last_end <= row_end + 0.5f);
    }
  }
  ```

### Tooltips (e221b77) — available now that the bump landed
Next step: add `.with_tooltip(...)` where labels ellipsize: file
rows (full path), commit rows (subject), repo header and tabs (repo path).
`UpdateTooltips`/`RenderTooltip` register through the existing
`registerUIPostLayoutSystems`/`registerUIRenderSystems` calls, so nothing else
changes. Everything else in the range is either free with the bump (wrap
memo, measure-by-advance, containment tolerance, bitset) or not used here
(grid, rect algebra, scrollbar colour, profiler, set_slider, blend mode).

---

### capture_impl.h staging-texture leak — RESOLVED upstream
Both staging paths now release their readback texture, so screenshots no longer
leak one GPU texture per capture in a non-ARC build. FloatingHotel still builds
`src/sokol_impl.mm` with ARC and wraps headless frames in an autorelease pool;
both are harmless, and neither is compensating for this leak anymore.

### Windowed startup waits — first-frame and font-memory parts resolved; AppKit/Metal floor remains
Measured 2026-09-10 on macOS 26.6.2 with temporary probes in
`vendor/sokol/sokol_app.h` (reverted). `Startup time` (graphics::run to
app_init done) is 510-740 ms warm; app_init itself is 10-60 ms. Where the rest
goes, per launch:

| phase | warm | notes |
|---|---|---|
| `[NSApplication sharedApplication]` | 120-200 ms | AppKit. A trivial ObjC program measures 220-670 ms for the same call on this machine, so this is the floor, not us. |
| AppKit launch → `applicationDidFinishLaunching` | 30-115 ms | AppKit |
| `NSWindow initWithContentRect` | 100-190 ms | AppKit. Drops to 75-95 ms inside a `.app` bundle with an Info.plist. |
| `MTLCreateSystemDefaultDevice` + MTKView | 3-12 ms | fine |
| `makeKeyAndOrderFront` → first `drawRect` | 65-160 ms | **sokol_app waits for MTKView's display link to deliver the first frame before calling init_cb.** |
| init_cb before app_init (`sg_setup`, `sgl_setup`, `sfons_create`) | 50-180 ms | sokol_gl compiles its Metal shader from source at startup; 2048² font atlas. Unchanged at -O2, so it is Metal, not CPU. |
| exec → `main` (dyld) | 180-600 ms at 32 MB, 50-76 ms at 4.1 MB | not in `Startup time` at all; logged as `Process start to main` |

Bundling does not change the total meaningfully, and `-O2` does not change
the AppKit/Metal phases. What `-O2` does change is exec→`main`: zig c++'s
default UBSan instrumentation plus no dead-strip made a 32 MB binary, and
exec→main scaled with it. `-fno-sanitize=undefined -Wl,-dead_strip` gives
17 MB (exec→main 400-490 ms warm); adding `-O2` (now the makefile default,
`make OPT=-O0` to opt out) gives 4.1 MB and exec→main 50-76 ms warm, next to
31-44 ms for a 51 KB control app. It also shrinks what Santa and Defender
hash on the first launch after every relink (that first launch was 5-25 s
under load at 32 MB; 760 ms exec→main at 4.1 MB).

With the -O2 binary at load ~95, windowed `Window+GPU init` is 460-640 ms
warm while the control Cocoa app spends 1.2-1.6 s in
`sharedApplication`+`NSWindow` alone, so what remains is AppKit and the
machine's daemons, not the app.
`sapp_set_icon` with the default icon is 0.1 ms; not worth a RunConfig knob.

- **First frame:** resolved upstream. The backend installs an early-draw
  observer before `sapp_run`, so FloatingHotel deleted its duplicate
  `metal_draw_first_frame_early` implementation. Its remaining
  `fh_install_startup_draw_timer` only drives repeated draws while the startup
  gate deliberately keeps the window unordered.
- **Font loading:** `load_font_from_memory` now exists on the Sokol backend.
  FloatingHotel has not switched its three TTFs to embedded bytes; doing so is
  an optional startup/packaging change, not a missing engine capability.
- **Suggested fix (backend.h):** ship the sokol_gl shader as precompiled
  metallib via sokol-shdc, or defer `sfons_create` until the first text draw.
  Worth measuring with probes in `setup_sokol_gl_and_fonts` first; the
  50-180 ms above is that function plus `sg_setup`.
- **Repro:** `./output/floatinghotel.exe .` prints `Window+GPU init: N ms`
  (main.cpp, time from graphics::run to app_init entry) next to the existing
  `Preload+fonts` / `Systems registration` lines.

## Feature Requests (Lower Priority)

### Native startup has no deferred-presentation API

- Boundary: the pinned Sokol `applicationDidFinishLaunching` creates the window, sets regular activation policy, and calls `makeKeyAndOrderFront` before the application's first frame. Neither `sapp_desc` nor the afterhours run configuration exposes start-hidden or present-when-ready behavior. Moving an already-visible window offscreen cannot prevent its initial flash.
- App workaround: `src/sokol_impl.mm` gates ordering and key status on the app's Sokol window class. An app-owned `NSApplication` subclass defers activation and regular activation policy. A timer draws the hidden Metal view until the active repository's initial refresh settles. A fence on the public Metal command queue completes before the window appears. Untouched inactive repositories start their initial reads only when activated.
- Verification: `nice -n 10 bash tests/check_startup_ready.sh` exercises restored tabs, welcome, and an invalid restored repository in real windows. Each case requires hidden and non-key state with a different frontmost application before presentation, then visible and key state afterward. The first scripted action clicks a control without an extra readiness wait. The restored case also switches away from an in-progress tab load and checks that untouched tabs never start their slow filters.
- Evidence: `/tmp/fh-startup-windowed-gate5.log` passes all three cases. Sokol's synthetic activation event sets `NSApp.isActive` before reveal even with prohibited activation policy, but `NSWorkspace.frontmostApplication` remains a different process. The test therefore checks actual system focus rather than that internal flag. No event suppression is needed.
- Maintainer request: expose deferred initial presentation and activation, keep hidden-frame callbacks available, and provide a ready-frame presentation operation after GPU submission. The app should not need the private Sokol window class for this lifecycle.
- Windowed E2E teardown constraint: direct `std::exit` after a frame can destroy Sokol's in-flight semaphore before GPU completion. The reproduced crash is `floatinghotel.exe-2026-09-11-224841.ips`. The app now stops workers, detaches the Git callback, retires image handles, and drains the public Metal queue before E2E teardown. This is an explicit-exit host integration requirement, not an ordinary window-close failure.

### E2E render checkpoint — RESOLVED upstream, adopted in the app click handler

Upstream UI commands now carry the render generation they were dispatched
against (`PendingE2ECommand::seen_render`) and wait through
`render_checkpoint_pending` until a newer frame has rendered. FloatingHotel's
`HandleLayoutClick` calls the same checkpoint before resolving a target.

That app handler still exists for a narrower reason: upstream click lookup is
single-match and fails on ambiguity, while FloatingHotel scripts deliberately
click the first visible match for repeated labels such as file names that also
appear in breadcrumbs and headers. It also provides `hover_ui`. A first-match
or nth-match upstream click mode would let the app handler be deleted; the
render-generation request itself is resolved. The item 01/item 41 screenshot
reproductions in the evidence log predate the checkpoint.

### E2E property assertions do not parse quoted values

- Status: reproduced during review item 21 on b385dc9.
- Reproduction: `assert_ui file_header_label "text=a-small.cpp  +1  (new file)"` fails with `unknown property '"text'`. The failing run is `/tmp/fh-item21.log`; the header itself is correct in the screenshot.
- Boundary: the default argument parser in `src/plugins/e2e_testing/runner.h` splits with `iss >> arg`. `assert_ui` has no quoted-argument branch, so `parse_prop_assertion` in `ui_commands.h` receives the leading quote as part of the property name.
- App workaround: use space-free property assertions for sidebar order, unit checks for ordering, and screenshot inspection for the multiword header.
- Maintainer request: share a quoted-argument tokenizer across commands, with tests for spaces, escaped quotes, and backslashes in property values.

### E2E text matching does not join adjacent styled spans — OPEN

- Status: reproduced in review items 03 and 06 on b385dc9.
- Reproduction: `expect_text "int answer = 42;"` times out on a visibly highlighted code line. `output/review-50/item-06.log` lists `int`, ` answer = `, `42`, and `;` as separate visible entries. The expanded-context fixture for item 03 similarly fails when its marker crosses a numeric token boundary.
- Boundary: `rendering.h::draw_text_in_rect` registers each styled run independently in `VisibleTextRegistry`.
- App workaround: assert individual spans plus cache state and inspect the composed line in a screenshot. Use a single-span marker when testing context expansion.
- Maintainer request: register the composed visible label for text assertions while retaining per-span bounds for hit testing.

### Grid-snapped placement can exceed the scrollable extent — OPEN

- Status: reproduced in review items 02 and 43 on b385dc9.
- Reproduction: at 1440×900, a source row requested at 22.5 physical pixels advances by 25 pixels after grid snapping. The page-end text assertion passed, but the screenshot stopped at line 14994 instead of 15000. The binary viewer reproduced the same issue: `/tmp/fh-peer43-dump.log` records a viewport at y94, height684, scroll4972, and the final `ff0` row at y830, outside the viewport.
- Boundary: `autolayout.h` snaps placement/cursor positions, including paths where a child requests `skip_grid_snap`; `ui/systems.h` computes scroll content size from unsnapped child heights. Virtual spacers and placed rows therefore disagree about the content extent.
- App workaround: calculate row heights with public `AutoLayout` grid rounding, and use the same grid for virtual viewport metrics. The binary viewer also accounts for its content padding. `output/review-50/item-02-grid.log` passes the actual viewport-bounds gate, and the inspected `item_02_page_end.png` shows line 15000.
- Maintainer request: share effective snapped extents between placement and scroll measurement, and make `skip_grid_snap` semantics consistent for sizes and positions.
- Horizontal reproduction in item 11: at 1440×1000, `commit_detail_scroll` had width1086 while its pixel-sized diff rows had width1088. Horizontal input moved the whole panel by two pixels. At 1440×900, the working-tree viewport had width1088 but its percentage-sized statistics row rounded up to1090, causing the same two-pixel scroll. Dumps are in `/tmp/fh-infra-scroll-probe.log` and `/tmp/fh-infra-working-scroll.log`; `/tmp/fh-infra11-width-red.log` contains the failing native width assertion.
- Horizontal workaround: keep the commit viewport pixel-exact with `skip_grid_snap`, and use the existing pixel width for the diff statistics row. `tests/review_50/item_11.sh` checks zero horizontal offset for short commit and working-tree content at resized window sizes, while a 4096-character line still scrolls to its end. Percentage widths should never round beyond their available parent width.

### E2E text assertions can match clipped or overscan rows — OPEN

- Status: reproduced in review item 02 on b385dc9.
- Reproduction: `expect_text END_OF_LARGE_FILE` passed while the final six lines remained below the scroll viewport. Screenshot inspection caught the failure that the text assertion missed.
- Boundary: text registration alone does not prove that the composed label is inside its ancestor clip rectangles.
- App workaround: `visible_source_line:15000` walks the source row's UI ancestors and requires its full vertical bounds to lie within the scroll viewport, after a render checkpoint. The corrected native test passes in `output/review-50/item-02-grid.log`.
- Maintainer request: offer a visibility assertion that intersects label bounds with ancestor clipping, separately from text existence assertions.

### Synchronized scroll views — RESOLVED upstream (dd579a4), and not needed here
`HasScrollView::sync_group` — give two or more views the same non-zero id and
scrolling any one moves the rest, on their enabled axes only.

floatinghotel does not need it: `render_side_by_side_diff` puts both sides in
*one* scroll view as two cells per row, so they cannot desync. Reach for
`sync_group` only if the two panes ever become separate scroll views.

### Virtualized list rendering — RESOLVED upstream (`virtual_list`), adopted
`imm::virtual_list(ctx, mk(parent), count, row_height, render_row, config)`
builds only the rows in the viewport (plus overscan) and reports the unbuilt
extent through `HasScrollView::unbuilt_content_size` so the scrollbar spans
the list. The sidebar file list and commit log use it as of 2026-09-10.
Measured with `bench_frames` (tests/run_stress.sh): 1000 files went from
2654 entities / 19.6 ms per frame to a flat ~5 ms at 100, 1000 and 5000 files.

### Retained texture components survive a texture-free immediate widget — RESOLVED upstream, adopted
`apply_texture` now tracks configured textures and clears `HasTexture` when a
later config omits one. FloatingHotel deleted its entity scan that stripped
retiring handles before `unload_texture`; image preview uploads also moved to
the public `load_texture_from_rgba`. A general safe-retirement policy for
textures still referenced by retained entities would be useful, but it is no
longer needed for this transition.

### Sokol frame pacing does not honor the requested frame rate

The Sokol backend defines `graphics::set_target_fps` as a no-op and does not
apply `RunConfig.target_fps`. Skipping the entire callback is unsafe because
mouse and keyboard edges are callback-scoped.

The app keeps input and state updates running each callback but skips idle
draws. `tests/review_50/item_13.sh` checks two 20-callback idle samples, each
with 4 rendered frames and 16 skipped frames, then exercises typing, Escape,
scrolling, filesystem refresh, an asynchronous file read, and resizing.
The verified run is `/tmp/fh-runtime13-followup-native.log`.

The backend should honor the requested frame rate or expose an event-aware
wait that preserves edge input. The app workaround does not change the vendor.

### Virtual-list metrics in Adaptive mode — RESOLVED upstream, adopted
`virtual_list` now documents row heights in `pixels()` units including Adaptive
zoom, carries the scale in its retained row metrics, divides generated row and
spacer heights back to logical size, and sets `skip_grid_snap` internally.
FloatingHotel deleted `src/ui/virtual_list.h`; all callers use
`afterhours::ui::imm::virtual_list` directly. The separate question of whether
flow placement fully honors `skip_grid_snap` remains under the grid-snapping
entry below; it is no longer a virtual-list unit mismatch.

### Font-size tiers and Adaptive zoom — RESOLVED upstream
Semantic font sizes now resolve through Adaptive scaling, and `Theme::text_scale`
provides the global text multiplier. FloatingHotel still mostly uses explicit
logical pixel sizes because code fonts, row heights and measurements share its
zoom factor; reverting those call sites to tiers is optional cleanup, not an
engine blocker.

### Keyboard-only focus rings — RESOLVED upstream
Focus modality is retained separately from grabs, `UIContext::focus_visible()`
is false for pointer-driven focus, and visual-focus computation uses it. The
app's thinner accent ring remains a theme choice, not a workaround.

### Per-component grid opt-out does not preserve exact flow spacing

The review-focus layout disables grid snapping through the existing application
styling setting. This keeps logical row heights and flow offsets consistent
through zoom. The `skip_grid_snap` limitation described above remains upstream;
no copied layout engine or vendor patch is needed for this workaround.

### E2E hidden assertions — RESOLVED upstream
`expect_no_ui <debug_name>` now asserts absence directly. FloatingHotel's
`ui_present:` property remains only because existing scripts use it; new
absence checks should use the upstream command.

### Text-area line widths at increased zoom — RESOLVED upstream, adopted
See “text_area lines overflow their own field” above. The app adapter described
in the original regression entry has been deleted.

### Text inset and resolved origin — RESOLVED upstream
Plain and styled label paths now share the resolved inset, the theme default is
zero unless an app chooses otherwise, and `resolved_text_origin` exposes the
origin/inset/font size used by the renderer. FloatingHotel's code rows set an
explicit 5px inset, so `diff_sel::content_x_offset` remains an app geometry
helper rather than a renderer workaround. Keep
`tests/check_text_highlight.sh` as the alignment regression.

### Temporary-entity query diagnostics during tab and zoom checks

The passing native tab and zoom checks still log `query will miss 1 ents in temp`.
See `output/text-highlight/review-pinned/tabs.log` and `zoom.log`.
`EntityQuery` emits this diagnostic when temporary entities exist and neither
`force_merge` nor `ignore_temp_warning` is selected. The message does not identify
the calling query or its component filters.

The caller is now identified. A debugger breakpoint at `entity_query.h:654`
stopped in the app's E2E UI-property getter at `main.cpp:1179`.
`output/layout-followup/query-lldb-matched.log` preserves the stack.
The app queries UI properties and pending commands between system ticks, so
they can merge pending entities before querying. They now request `force_merge`
instead of silently excluding those entities. The diagnostic is not suppressed.
Including the query origin or filters upstream would still make this warning
easier to trace without a debugger.

### Text-area line height — RESOLVED upstream
`text_area` resolves `text_area_line_height` through the active scaling mode
instead of reading the raw `Size` value. The app's `pixels(...)` call sites are
now ordinary configuration, not a workaround.

### Divider hit area — expansion landed; active-drag capture still needs work
`imm::divider` now pads a sub-12px divider's hit rect through `HasHitExpand`.
FloatingHotel tried replacing its 16px commit-splitter band with a true 1px
divider on `5383fc6`; `tests/sidebar_splitter.py` then failed the top-clamp
reversal (drag to y=0, back to y=20: the divider stayed at its minimum). The
app therefore keeps the 16px band with a centered 1px child for now. Upstream
still needs active divider drags to retain pointer capture after the pointer
leaves the expanded hit rect and crosses other hit targets.

### Small-window sidebar rows shrink below their labels

The related splitter checks still report overflow from `sync_caption` and
the Changes, Staged, Untracked, and Refs tabs. In the Files sidebar at 800 × 480,
the sync row shrinks to 6.6 pixels around a 14.7-pixel label. The mode-tab row
shrinks to 5.3 pixels around 16-pixel labels. These warnings also occurred before
the splitter change. Functional assertions pass, but that does not establish
correct rendering at this size.

Reproduce with `tests/e2e_scripts/flow_sidebar_scroll.e2e`. Current evidence is
`output/commit-splitter/related.log`; earlier evidence is
`output/layout-followup/final-flows.log`. No workaround is included in the
splitter fix. The next investigation needs to distinguish app height budgeting
from framework flex shrinking before assigning an upstream bug.

The default-font check also reproduces this at 1280 × 800 and 140% zoom:
the mode-tab row has 16.9 physical pixels for 26.7-pixel children. It occurs
with the old 14-pixel code font before any font changes. See
`output/larger-font/native-before.log` and
`output/larger-font/before/default_font_zoom.png`. Code text sizing does not
change the sidebar's font or row budget. No workaround is included in the
default-font change.

### Skipped redraws retain earlier UI draw commands — RESOLVED upstream, adopted
`BeginUIContextManager` now clears `render_cmds` and the stale last action at
the start of each UI update. FloatingHotel's `ClearPendingUIDraws` system has
been reduced to resetting its own reading-performance counters; it no longer
touches the framework draw queue. Keep `tests/check_text_flicker.sh` as the
idle-redraw regression.

### Startup reports a missing toast singleton

Both the failing and fixed flicker runs log a missing
`afterhours::toast::ToastRoot` singleton during startup. The diagnostic does not
identify the caller. The app registers toast singleton enforcement before its
UI systems, so identifying the earlier lookup needs a separate trace. This
warning remains unresolved and is not evidence of the flicker cause. See
`output/text-flicker/after/run.ENEZrH/native.log`.

### UI dumps and computed spacing — upstream part resolved
Framework `dump_ui` now writes subpixel geometry plus resolved padding, margin
and gap. FloatingHotel's JSON sidecar remains authoritative for its tests
because it also records parent links, full text and reading-performance fields;
that is an app schema choice now, not a missing spacing dump.

### Immediate plain labels and text inset — RESOLVED upstream
The plain, styled and batched paths now position through the same resolved
inset. Sidebar/code callers that need a margin set it explicitly; no renderer
workaround remains.

### Toast sizing and scrollbar layering

The framework toast uses a fixed-height label, retains its initial computed
size after resize, and has no close control, hover pause, duplicate handling,
or visible-stack limit. The app now keeps the existing `toast::send_*` API but
replaces its update/layout systems with `ui::ToastSystem`. Cards use logical
sizes, 12-pixel padding, 28-pixel dismiss targets, wrapped scrollable messages,
and at most three visible cards. Repeated messages combine. Informational
messages pause under the pointer; warnings and errors require dismissal.
Hidden queued messages do not expire before being displayed.

App notification producers also used a single pending string. Simultaneous
Git completions overwrote earlier messages and all severities became short
info notices. A typed pending-notice vector now preserves each result and its
severity. No framework changes were required for that app bug.

The first visual check showed sidebar scrollbars painted through the cards.
`UIPluginRenderBridge` runs `RenderScrollbars` after all UI layers, so a higher
toast layer cannot prevent this. The app now holds layers 100 and above,
draws ordinary UI and its scrollbars, then draws floating UI and its own
scrollbars. This also keeps modal surfaces above underlying scrollbars.
It reuses the framework renderers without copying or editing vendor files.
Before and after: `output/spacing-audit/toasts/toast_zoom_small.png` and
`output/spacing-audit/toast-final/toast_zoom_small.png`.

The unused `ToastRoot` enforcement is removed because `send_*` creates
independent entities and the app renderer does not need that root. The startup
missing-singleton warning above is absent in `output/spacing-audit/toast-final.log`.
Tests cover duplicate notices, hover pause, expiration, persistent warnings and
errors, dismiss clicks, wrapping, and small-window zoom. Text-highlight and
idle-flicker checks also pass after the render-order change.

### macOS menu integration

Afterhours does not provide a native macOS menu adapter for this app. The app
adds a small AppKit bridge, leaving Sokol's delegate and window-presentation
logic intact. Native selections enqueue command IDs, and the app rechecks
enabled state before executing existing menu actions. Repository mutations
remain disabled in Review Workspace. The in-window fallback stays available
for non-macOS and ordinary headless tests, above repository tabs.

Native menu equivalents can consume keys before Afterhours text inputs see
them. The existing menu Copy action handles diff selection, while keyboard
Copy also handles text fields. Cmd+Enter has separate commit and feedback
contexts. The bridge therefore displays app shortcuts but lets their events
continue to Sokol; only native Quit handles its own equivalent. This avoids
duplicating or stealing the existing keyboard actions. Physical keyboard
delivery through an open native menu remains a manual verification item.

The bridge owns Objective-C objects explicitly and compiles without ARC.
Sokol keeps ARC enabled for screenshot readback cleanup. Native tests create
an application with prohibited activation and no windows. The app's headless
integration opts into that same setup with `FH_NATIVE_MENUS=1`.
### Tab keyboard injection bypass

The native-menu integration check exposed repository-tab shortcuts reading raw
graphics key state while other app shortcuts read Afterhours input state. The
headless runner injects into the latter and parses CMD as Control. Switching only
the wrapper did not fix the test. Tab shortcuts now use the input wrapper and
accept either Command or Control key, like the other app shortcuts. The test
exercises creation and closing rather than replacing keyboard checks with clicks.

### Wrapped labels disagree with their drawing inset

The visual follow-up found the last letters clipped in a long error toast even
though the card passed its geometry checks. Plain text wrapping uses the
configured inset, which defaults to zero, but drawing each resulting line uses
the hardcoded five-pixel margin described above. Toast and feedback body labels
now request a five-logical-pixel inset and measure against that narrower width.
This prevents clipping without changing code rendering or selection coordinates.
The framework should honor the supplied inset in its single-line path.

### Fixed sidebar controls shrink below their children

At 900 by 640 and 140% zoom, the Files sidebar had a four-pixel tab row around
21-pixel buttons and a zero-height review-progress row. Explicit desired heights
alone did not prevent flex shrinking. The upper Files section now has a scroll
viewport and an inner body with a computed minimum height. Controls retain their
heights while commit history keeps its allocated area. This avoids moving the
global footer or compressing labels. `tests/check_files_controls_spacing.py`
checks the resulting hierarchy from captured JSON.

### Empty button padding and default card rounding

For buttons, `Padding{}` means unspecified, so the framework inserts sixteen
pixels on every edge. A 28-pixel fold or tab-add button then has negative inner
space. The affected controls now specify an explicit zero edge to disable that
fallback; ordinary preset buttons use eight-pixel horizontal padding.
`tests/check_control_padding.py` rejects padding larger than its control.

A feedback card or editor without an explicit radius rendered as a large capsule.
Both now request the same six-pixel radius and corner flags used by toast cards.
The first feedback screenshots remain in the audit output for comparison.

### E2E and JSON coordinates stop at the nearest scroll ancestor

Rendering and real hit-testing correctly accumulate every ancestor's scroll
offset. `testing::ui_commands::get_screen_rect` still stops at the nearest
scroll or clipping container, and skips offsets entirely for scroll containers
themselves. After introducing the Files controls viewport, a file visibly near
y434 was reported near y483. The scripted click hit the sidebar splitter.

The app's JSON dump and test click adapter now use the renderer's existing
`detail::apply_scroll_offset` and `compute_intersected_clip_rect` helpers. JSON
includes both the full screen rectangle and its visible clipped rectangle.
Named/text clicks choose a visible portion and fail when the target is fully
clipped. The adapter is registered before the framework click handlers; it does
not alter production input. Older XML dump/assert helpers still use the framework
coordinate path, so JSON is the authoritative nested-scroll geometry here.
The failing captures are `output/spacing-audit/final-verified`; they are not
successful final verification despite that directory's name.

### UI labels may contain non-UTF-8 repository bytes

The binary-file fixture exposed an app diagnostic bug: UI labels can retain raw
file bytes, and strict JSON serialization threw from the deferred screenshot
callback. The app now uses the serializer's replacement policy for malformed
UTF-8. Geometry remains exact, while invalid text bytes become U+FFFD. The crash
is recorded in `flows-verified.log` and the macOS report
`floatinghotel.exe-2026-09-12-045621.ips`. A JSON diagnostic should not assume
that every label string is valid Unicode.

### Legacy zoom flows still report sidebar overflow

The passing 74-flow run in `output/spacing-audit/flows-final.log` still reports
layout overflow for sidebar mode tabs and sync controls in a legacy zoom flow.
Resolved, and app-side: the sidebar's Git controls sized their rows with
`h720()` (a fraction of the window height) while their fonts were zoom-scaled
`pixels()`, so at 200% the text outgrew rows that had not grown with it
(`flow_light_theme` reproduced it: rows 2-5 px tall). Those rows now use logical
pixels and the controls scroll when they do not fit; `tests/sidebar_typography.py`
checks them at 100/140/200. `h720`/`w1280` remain in presets, main content,
the command log and the toolbar.

### Panel padding exposed a stale frame-clear color

The inset main panel does not paint its outer spacing. Afterhours leaves those
pixels at the frame-clear color. Both app draw paths still cleared to the old
RGB 30/30/30 instead of the active window theme, creating a gray strip beside
the commit view. Both now clear with `theme::WINDOW_BG`. This was an app palette
mismatch, not a framework defect. The pixel check in
`tests/check_panel_background.py` fails on the old screenshot and checks both
side gutters in new captures.

### Tree chevrons cannot rely on the current font atlas

The initial styled tree used Unicode disclosure arrows, but the open arrow was
invisible in the native screenshot. The current font path did not draw that
symbol; this does not establish whether the cause was face coverage or atlas
handling. Folder disclosure now uses a small rotated two-sided border,
so its shape does not depend on text glyph coverage. Folder and search markers
also use native geometry. The first tree checker additionally used the wrong
JSON field name, `label` instead of `text`. That test error was corrected before
accepting the tree unit.

The row's presence, text, and nonzero bounds all passed before the missing arrow
was caught visually. `tests/check_mock_tree.py` now checks the disclosure's
captured pixels too. Layout validity alone does not prove a glyph was drawn.
The Sokol glyph-coverage API limitation is recorded above under
`Font codepoint coverage helpers are raylib-only`.

### Wheel input ignores ancestor scrolling and clipping

`HandleScrollInput` tests the mouse against `cmp.rect()` rather than the rendered
rectangle. In the Files view, the outer controls viewport moves the inner file
list up by about 48 pixels. Scrolling over a visible row near y420 did nothing
because the framework tested the inner viewport near y442 instead.

The local `HandleVisibleScrollInput` keeps the existing easing, axis, speed, and
clamping behavior, but uses `apply_scroll_offset` and the intersected ancestor
clip before accepting wheel input. Registration replaces only the framework's
scroll-input system inside its post-update bridge. This depends on the public
bridge system list, so upstream changes to that registration need review.
`mock_tree_working.e2e` exercises the previously unresponsive visible area.

The installation itself is fragile. `registerUIPostLayoutSystems` walks
`SystemManager::update_systems_`, finds `UIPluginPostUpdateBridge<InputAction>`,
then replaces its `HandleScrollInput<InputAction>` child. If an upstream change
keeps those types available but changes registration, the search can find no
matching child and silently leave the workaround uninstalled. A supported
replacement hook with an explicit success result would avoid this dependency.
This is a risk in our adapter, not another reproduced scrolling failure.

### Nominal font pixels do not give browser-equivalent type size

The same commit title in a 1440-by-1000 mock capture has roughly 21 pixels of
foreground height, versus 16 in the native capture at a nominal 22-pixel font.
The native Roboto/fontstash path and the mock's browser system-font path use
different metrics. The commit headline is calibrated visually at 28 native
pixels. Code retains the user's 16-pixel setting. Matching CSS font-size values
alone is not sufficient evidence of visual parity.

### Font metadata is duplicated across label and layout components

The first font-aware JSON dump read `HasLabel::font_name`. It remained
`__unset` even when `ComponentConfig::with_font("mono", ...)` was applied.
The renderer selects the font from `UIComponent::font_name` instead. The
dump now reads that field, and records each styled text span separately.
This was an app diagnostic mistake exposed by overlapping framework fields,
not a failure to render the selected font. The diff check caught it before
the final unit was accepted.

The same native/browser font-metric difference also made the first 13-pixel
diff captions look too small. File paths now use 15 native pixels and hunk
captions use 14, while code retains the user's 16-pixel setting. File-fold
arrows use the same geometry workaround as the tree disclosure controls.

### Styled labels bypass ellipsis truncation

The renderer computes `display_text` for ellipsis, but the styled-run path
then draws the original `HasLabel::spans` with unbounded width. A long hunk
caption or path can therefore paint underneath neighboring controls despite
`TextOverflow::Ellipsis`. Diff captions now sit in an explicit clipped
container, and file-title groups clip before the action cluster. This keeps
the controls clear but truncates styled text without an ellipsis. Upstream
should truncate the styled runs while retaining their colors and weights.
Quick Open also uses filename-first labels inside its clipped list so a long
parent directory cannot hide the filename. Its full-path tooltip supplies the
clipped portion. Native verification checks colored match spans and uses the
row's semantic path because text assertions see individual runs.

### Expanding immediate-mode controls can warn before the next layout pass

The browsing regression run logged `review_file_filters` extending beyond
`commit_find_host` when Options opened. The host height was calculated before
the button changed `diffOptionsOpen`; the child rows saw the new state in the
same frame. The next frame calculates the expanded height. This is an app
ordering issue exposed by immediate-mode construction, not a persistent
overflow or proof of a framework defect. A layout invalidation or deferred
state-change convention would make these transitions easier to reason about.

### Mixed-height diff virtualization requires duplicate size accounting

The adopted `virtual_list` covers fixed-height sidebar rows. Our diff also has
file headers, hunks, code rows, comments, footers, and folded sections with
different heights. `src/ui/diff_renderer.h::DiffViewport` therefore tracks its
own running extent through `built`, `skipped`, and `flush`.

Adding a visible footer requires both a UI height and a matching `vp.built`
call. Moving spacing across a `continue` changes whether folded or binary files
receive it. During the parity pass, file spacing moved before each subsequent
file so those branches receive the same gap. The two-folded-file capture and
`tests/check_mock_file_spacing.py` verify the resulting 14-pixel separation.

This is an integration maintenance cost, not a reproduced new framework bug.
A variable-height virtual-list API with one source of row extents would reduce
the risk of scroll geometry diverging from rendered geometry.

### The host refresh wait times out without failing its command

This is an app test-harness footgun, not an Afterhours Git-loading defect.
`src/main.cpp::HandleWaitForRefresh` consumes the E2E command before its
asynchronous work finishes. The host later logs a warning after 30 seconds
and lets the runner continue, even if the repository is not ready.

In `output/mock-parity/clipping-check/mock_diff.log`, that warning preceded
missing commit and button targets. Git history arrived after about 57 seconds.
The test was running during compilation, but that does not establish why Git
was slow. The resulting screenshots were rejected, and the native checks
passed after compilation finished.

The wait is not a readiness guarantee after its timeout. A pending-command
barrier that reports timeout failure and stops dependent UI commands would
avoid misleading follow-on errors. The current host timeout behavior remains
unchanged. The related render-generation requirement is documented above under
`E2E target lookup needs a render checkpoint after cached view transitions`.

### Dock resizing needs different geometry from the panel animation

The dock sidebar remained 280 pixels wide in a 352-pixel window, leaving a
72-pixel empty strip. The app's `review_layout::sidebar_width` capped both the
settled dock and the animated sidebar at the saved width. This was an app
layout error, not an Afterhours rendering defect.

The settled dock now fills the logical viewport. Native window resizing updates
the remembered sidebar width using the current zoom scale. Panel animations
retain their fixed sidebar width, and the animation state takes precedence over
the collapsed state during closing. The old four-pixel collapse inset was
removed so repeated open-close cycles do not increase the remembered width.

The integration footgun is the distinction between physical window dimensions,
zoom-adjusted layout dimensions, and transient animation dimensions. Recording
the animated width as a new user preference would reintroduce sidebar relayout.
The width is retained in memory, not written to settings on every resize frame.

`tests/review_focus/dock_resize.e2e` captures dock resizing at 300, 352, and 480
pixels, zoom, an opened commit, and return to the dock. Its JSON geometry is
checked by `tests/check_dock_resize.py`. `test_review_layout` covers the pure
width calculations, including fixed width during animation. The headless host
deliberately disables physical window animation, so these checks do not exercise
native OS resize events or the native-only width-memory update.

Verification passed on 2026-09-12: six layout unit tests, the dock geometry
checker with both menu configurations, and the commit-splitter drag regression.
The before capture failed at 280 of 352 pixels; the after capture fills all 352.
Local captures are in `output/dock-resize/{before,after,native-menu}`. The
after-capture check is `nice -n 10 python3 tests/check_dock_resize.py output/dock-resize/after`.

### Native resizing must happen outside the Metal draw callback

The user reported a magenta flash during panel animation and a return to the
default window width. The app resized its `NSWindow` from `LayoutUpdateSystem`
inside the draw callback. Sokol's `_sapp_macos_frame` explicitly warns against
updating dimensions there because Metal render targets can have different sizes.
`setFrame` triggers `windowDidResize`, which updates the drawable dimensions.
Pinning the layer to the top-left did not fix that ordering problem.

The hidden native test in `tests/native/window_resize.mm` failed before the fix
with `window dimensions changed inside the Metal draw callback`. This proves
the unsafe dimension change, not the exact color of the user's transient frame
or a hardware fault. Its Metal validation run uses the real Sokol view and the
app's resize implementation, rather than the headless render target.

The app now queues and coalesces resize requests on the main dispatch queue.
The resize runs after the draw callback returns, uses Sokol's actual window,
and does not request synchronous AppKit display. Rendering and first-window
presentation wait while a resize is pending. Opening and closing perform one
resize with no tween. The review panel remembers its own logical width, so
changing the dock width does not restore an obsolete total window width.
This replaces the animation-specific workaround in the preceding entry.

### Afterhours can own the Objective-C++ window helper

The C++ API `graphics::set_window_size` already exists. Its Sokol backend
implements headless resizing but logs `@notimplemented set_window_size` for a
native window. `set_window_min_size` is also unimplemented. App-owned
Objective-C++ fills that gap today.

The useful upstream change is a C++ window API backed by one platform-owned
implementation. The public header can remain ordinary C++. A private macOS
helper compiles with the Sokol implementation, like the existing capture helper.
The app should not need AppKit types or Objective-C syntax to request a resize.

The resize contract needs these guarantees:

- Requests made during update or drawing apply between draw callbacks.
- Several pending requests coalesce to the latest size.
- Size units are content-area points, independent of Retina framebuffer pixels.
- A pending or completed resize is observable so callers do not draw a mismatched
  layout or present the first window before its requested size is ready.
- Top-left anchoring and minimum content size are explicit options.
- Native tests cover actual drawable sizes. Headless resizing alone is not proof
  that AppKit and Metal agree.

First-ready-frame presentation belongs in the same backend-owned window support.
The app currently suppresses activation and window ordering with Objective-C
runtime hooks. A supported deferred-show option would remove those hooks too.

The native test initially stalled because hidden `MTKView::draw` did not drive
the callback. Making that hidden window main also raised an AppKit exception.
The test now drives the hidden view and provides the old implementation's window
lookup without activating it. These were test setup errors, not proof of a
framework rendering failure. An external process timeout prevents a stalled
native check from blocking the suite. The runnable check is
`nice -n 10 bash tests/check_native_window_resize.sh` after an `OPT=-O2` build.

The first passing-frame run exposed another test teardown mistake: `std::exit`
destroyed Sokol's Metal semaphore while GPU work was still in flight. The crash
report identified `Semaphore object deallocated while in use` in the Sokol
backend destructor. The probe now fences submitted GPU work and calls
`sg_shutdown` before exit. This is lifecycle misuse in the probe, not evidence
that the resize itself failed.

Verification passed on 2026-09-12: the native probe completed six coalesced
resizes and 28 matching Metal frames at each of 1x and 2x DPI, with Metal API
validation enabled and no visible window. Six layout unit tests, the dock JSON
geometry check, the commit-splitter regression, and the 15-frame text-flicker
check also passed. Logs and app captures are in `output/window-resize`.
The native probe tests the resize boundary, not a visual reproduction of the
user's exact magenta flash in the full app.

### A hover-styled row needs to own its click target

Review's changed-file rows used a hover-styled `div` around a transparent
filename button. Afterhours assigns `hot` to the clickable child and applies
the hover fill only to that exact entity. The parent row never highlighted,
while its icon, change counts, and padding were not clickable. The selected
file stayed highlighted even when the pointer moved onto a neighboring file.

The fix makes the entire row a button and its filename an ordinary label.
The row now owns both hover and activation, as the Files-tab rows already do.
This is an app composition mistake exposed by exact-entity hover behavior,
not evidence that pointer coordinates were shifted by one row.

`tests/review_focus/tree_hover.e2e` captures hover over the filename, icon,
and counts, and tests activation through the icon and count areas. Before the
fix, `tests/check_tree_hover.py` failed because the row edges did not highlight;
the icon-click check also failed to select README. The pixel check compares both
edges of the hovered row and verifies that neighboring file rows stay unchanged.
Run the E2E script with `FH_NATIVE_MENUS=1`, then run the checker on its capture
directory. A framework warning for hover-styled, non-interactive containers
would help catch this mismatch, or an explicit hover-within option for composite
controls could make the intended behavior available.

After the fix, all five hover comparisons and the icon/count activation checks
pass. The existing row-coordinate test also passes at 100%, at 140% zoom, and
after scrolling. Captures and logs are in `output/tree-hover`.


## Reusable upstream candidates from the 2026-09-12 source audit

Source baseline: floatinghotel `4ef6c4e`, Afterhours
`b385dc993f7f90cac63346514542dc33429a814d`. The inventory covered every file under
`src`, including UI and ECS systems, utilities, platform code, Git readers,
settings, and review persistence. Detailed framework review followed the shared
workarounds into the vendored APIs. This was a source audit with selected unit
checks, not an exhaustive runtime test of every app feature or graphics backend.

These requests extend existing Afterhours components or propose optional
utilities. Git revision resolution, review progress, comments, document identity,
and navigation policy remain app-owned. The atomic-save helper has since landed. Remaining durability and adoption work
is recorded below and in [the active triage list](../triage.md#afterhours-work).

The first upstream fixes to prioritize are already documented above: nested-scroll
coordinates and wheel input, styled-label ellipsis, mixed-height virtualization,
texture retirement, and native window resizing. They have concrete app evidence
and benefit menus, inventories, scene editors, log viewers, and asset browsers.
The entries below cover additional reusable code rather than reopen those bugs.

### U1. Bounded background jobs with cancellation and foreground capacity

**2026-11-26 status:** `afterhours::jobs::Pool` now exists, but its
owner-thread callback/drain model lacks pollable futures, exceptions,
caller-supplied rejected values, non-cancellable jobs and this app's
shutdown-drains-accepted-work contract. `src/util/async_task.h` therefore
stays; the remaining request is those semantics, not a thread pool.

`src/util/async_task.h` implements `Executor` and `Task<T>` because content reads,
search, image decoding, and refresh work must leave the UI thread. The executor
has separate foreground and background queues, bounded admission, cooperative
stop tokens, and a reserved foreground worker when more than one worker exists.
`src/ecs/async_git_refresh_system.h` also owns the task lifetimes of repository
tabs. Asset decoding, thumbnail browsers, and game editor imports need the same
scheduling behavior.

No worker executor or task abstraction was found in the vendored
`vendor/afterhours/src` implementation. The proposed optional jobs utility should
return an explicit accepted, rejected, cancelled, completed, or failed outcome.
It should support nonblocking polling, bounded queues, priority reservation, and
shutdown that cancels reads while draining explicitly accepted must-complete
work. Workers return owned values; they must not mutate ECS entities.

A small owner token and request generation can accompany completions so a main
thread consumer can reject a result after its view or scene is replaced. The
host still defines the document or asset key. This is not a request to move Git
selection state into Afterhours or to create one executor per widget.

Current workaround: the app-owned executor and request checks. Verification in
this audit: all three tests in `tests/unit/test_async_task.cpp` passed after a
fresh build, covering reserved capacity, queue bounds and priority, and shutdown.
The proposed owner-token adapter does not exist and has not been tested.

### U2. File-change subscriptions for asset and document reload

**2026-11-26 status:** `afterhours::file_watcher::Watcher` now provides the
subscription/result/drain shape, but its macOS kqueue backend recursively
opens an FD per directory and regular file. FloatingHotel keeps FSEvents until
that API gains a coalesced recursive backend suitable for large repositories.

`src/platform/file_watcher.h::FSEventsWatcher` owns a macOS run-loop thread,
retains the loop through shutdown, watches multiple roots, and returns paths
with `mustRescan` after dropped events. `src/ecs/file_watcher_system.h` drains
those events and applies an app-specific cooldown. This is reusable platform
work for shader reload, texture import, localization files, and editor projects.

Afterhours already has `ui::theme_io::HotReloadTheme` in
`vendor/afterhours/src/plugins/ui/theme_io.h`. It polls one file's modification
time. `plugins/files.h` provides resource paths and reads, but no general watch
subscription or event queue. Theme reload alone does not replace the app watcher.

An optional file-watch API should accept multiple roots, return an owned
subscription, and drain path events on the caller's thread. It needs explicit
unsupported and startup-failure results, a bounded queue that reports rescan
when events are dropped, and a generation to discard events from an old
subscription. Shutdown must join safely even if stop races initial startup.
Debouncing policy and Git change classification remain with the app.

Current workaround: FSEvents on macOS and `NullWatcher` elsewhere. The null
implementation silently produces no events, so it is not portable verification.
Status: source-reviewed only. The existing `wait_for_file_change` E2E adapter
can verify app delivery; this audit did not run native watcher lifecycle tests
or tests on Linux or Windows.

### U3. A reusable cache with a byte budget and observable ownership

**2026-11-26 status:** `afterhours::ByteCache` landed with LRU eviction and
stats. Its cost function sees only the value, so it cannot charge key/index
overhead or keep accounting for shared values retained after eviction. The app
caches stay until that accounting contract exists.

`src/util/byte_cache.h` provides an LRU cache used by
`src/ui/token_cache.h` and `src/git/blob_page_cache.h`. The patch cache in
`src/git/commit_patch_cache.h` separately implements byte accounting and eviction
for a structured key. Token, blob-page, and patch defaults are 4 MiB, 32 MiB,
and 32 MiB respectively. Similar budgets matter for game thumbnails, decoded
assets, and long-running editor sessions.

Afterhours already has count-bounded text measurement caches in
`vendor/afterhours/src/core/text_cache.h` and `vendor/afterhours/src/measure_memo.h`, plus a wrapped-run cache in
`vendor/afterhours/src/plugins/ui/text_selection.h`. The missing reusable contract is a cache for
host-owned values with explicit byte costs, not LRU caching itself.

The utility should accept the key type and a cost function, reject oversize
entries, define replacement and eviction behavior, and expose hits, misses,
evictions, and accounted bytes. It must state whether values are copied or
shared and whether accounting includes bookkeeping. Shared references can keep
a value alive after eviction; cache occupancy must not be reported as total
process or GPU memory. Synchronization can remain an explicit wrapper, as it
is for the app's blob cache. GPU retirement is the separate existing entry.

Current workaround: the three app cache classes. Verification in this audit:
both `test_byte_cache` tests and both `test_token_cache` tests passed after fresh
builds. They cover eviction, replacement, oversize values, token reuse, and
continued rendering of uncached long lines. These checks do not measure RSS or
establish a need to increase any existing budget.

### U4. Public CPU image decoding and render-thread texture upload

**2026-11-26 status:** public CPU `decode_image`/`image_size` and
`load_texture_from_rgba` landed; FloatingHotel adopted the upload half. Its
decoder stays because it also handles WebP/AVIF/ICO through ImageIO,
cancellation, and a shared decoded-byte reservation across concurrent workers.

`src/git/image_content.cpp::decode` checks encoded size and image dimensions,
reserves a shared decoded-byte budget, and returns owned RGBA pixels from a
worker. `src/ui/image_diff.h::poll` uploads those pixels on the UI thread through
`afterhours::metal_texture_detail::load_texture_from_pixels`. The split is useful
for asset streaming, thumbnails, and procedural textures, but the app reaches
into a Metal-specific detail namespace to perform the upload.

`vendor/afterhours/src/backends/sokol/drawing_helpers.h` already implements that
pixel upload and uses it for file-backed textures. This is a public API and
ownership gap, not a claim that Sokol cannot upload image data. A portable
`DecodedImage` owner and a public texture-from-pixels operation would let hosts
use the same decode/upload split without backend internals.

The decode operation should accept encoded bytes, dimension and decoded-byte
limits, and cancellation, then return either owned pixels with a known format
or a structured error. Texture upload must state its required thread, preserve
filtering options, and return a clear failure. Decode cancellation and upload
failure must release the owned allocation. Shared decode accounting should be
separate from GPU accounting; decoder scratch allocations are not covered by
the app's current RGBA reservation.

Current workaround: app-owned STB decoding, limits, and Metal upload. Status:
source-reviewed. Existing checks are `tests/unit/test_image_content.cpp` and
`tests/review_50/item_12.sh`; they were not rerun in this documentation audit.
Safe retirement of an already-rendered texture remains the earlier reproduced
`Retained texture components survive a texture-free immediate widget` entry.

### U5. Focus-scoped shortcut routing over the existing input actions

**2026-11-26 status:** `with_shortcut` / `HasShortcuts` and focused dispatch
landed, which covers widget-local bindings. FloatingHotel still routes most
commands through its semantic focus owner because reader/tree/history scopes
and native-menu commands need one ordered host dispatcher; that is now phased
app adoption rather than a missing primitive.

`src/ecs/main_content_system.h` polls raw key codes for Find, Quick Open, and
review commands, then checks text-input focus, menu state, source-tab state,
and comment composition independently. `src/ecs/tab_bar_system.h` and
`src/ecs/menu_bar_system.h` handle more shortcuts. A game with chat, inventory,
and a pause menu has the same conflict between local and global commands.

Afterhours already consumes mapped actions through `UIContext::pressed` and
`pressed_or_repeat`. It also exposes input gates, `focus_in_subtree`, and
`ConsumesDirectionalInput`. The missing integration is an ordered route for
host shortcuts through those scopes. Raw key polling in app systems bypasses
that route, so the current app pattern is also adoption debt.

Extend mapped actions with a dispatcher that offers a chord to the focused
control, then its containing scopes, then the application scope. An action
consumed at one scope must not run again in another system during that frame.
Modal gates and repeat behavior need to apply to the same dispatch. Native
menu actions and injected tests should use the same host command callback.
Afterhours need not know which command means Back, stage, or pause.

Current workaround: scattered focus checks and duplicated modifier handling.
Status: source-reviewed design request. No new shortcut conflict was reproduced
in this audit. Acceptance should include typing in a text field over a reader
and a game canvas, nested dialogs, held modifiers, and one dispatch per frame.

Step 37's source-action menu reproduced a raw-key/widget-action collision:
Down moved framework focus behind the menu, then Enter activated a document
tab instead of File history. The app now consumes widget navigation, activation,
and repeat actions after `BeginUIContextManager` whenever its context menu is
open; the menu continues to read its own raw keys. The reusable need is scoped
input ownership that covers both raw input and framework widget actions. An
inventory or pause menu should not have to win an execution-order race against
the controls underneath. Evidence is in the step-37 source-header replay.

Step 38 reproduced the pointer equivalent: clicking Bookmarks selected the
source line underneath the menu, changing the line offered by Add bookmark.
The app now installs a context-menu input gate for the entire frame, and its
custom text-selection hit testing checks that gate. The gate also covers the
frame in which a menu closes, preventing the closing click from reaching code.
Custom widgets must honor the same input ownership as framework buttons.
The 200% replay also found the app testing unclipped code-row rectangles;
scrolled-out text could receive clicks over the header. Selection now uses the
existing intersected-clip helper. This was application adoption debt, not a
missing framework clip API.
The same menu owned its arrow selection but left rows in the framework's tab
order, producing a focus outline on a different row from the highlighted choice.
Menu rows now use the existing `with_skip_tabbing(true)` option. Composite
widgets should have one navigation owner; this is another adoption fix.

### U6. Focus return that survives rebuilt controls and closed scopes

**2026-11-26 status:** generation-checked `capture_focus()` /
`restore_focus(EntityHandle)` and `FocusProxy` landed. FloatingHotel uses the
proxy path through ordinary `set_focus`; its semantic repository/document/
region resolver remains necessary for rebuilt controls and closed scopes.

The original app focused picker inputs by entity ID and used root focus or
separate flags when a view closed. Menus and feedback panels had separate
open-state handling. Returning to the correct invoking control would also help
inventory popovers, settings dialogs, and game editor inspectors.

`vendor/afterhours/src/plugins/modal.h::Modal` already stores
`previously_focused_element` and restores that raw entity ID when the modal
closes. `ui/menu.h::HasMenuState` stores only the previous open flag. Thus the
request is to unify focus return and validate its destination, not to add basic
modal focus trapping or restoration.

A focus-return token should identify its UI collection, owning scope, and
logical control, with a host resolver for a control rebuilt under a new entity
ID. Closing an overlay should resolve that token only if its owner still exists
and the target is eligible for focus. Otherwise it should use an explicit
fallback in the current scope. Nested overlays need a stack, and dismissal by
an outside click must not steal focus back from the newly clicked control.

The app now stores semantic region/control targets, resolves rebuilt controls
after layout, and validates repository owner, document, and navigation generation.
`tests/focus_return.py` verifies normal return, closed invokers, repository
switches, and typing after restoration at three zooms. The original audit was
source-only; step 14 adds runtime evidence for the app workaround. The framework's
modal token remains a raw entity ID and still needs the reusable API described
above.

### U7. Logical scroll anchors resolved after matching layout readiness

**2026-11-26 status:** keyed scroll anchoring, `virtual_list_keyed`,
`scroll_to_item`, `virtual_list_range` and `scroll_to_entity` landed.
FloatingHotel adopted `scroll_to_entity` for rendered tree rows. Cross-document
logical anchors with explicit layout-generation readiness remain app-owned.

`src/ui/reading_position.h::remember_reading_position` stores pixel offsets by
view key, writes `scroll_offset`, `scroll_target`, and `last_eased_offset`, clears
`anchor_child`, and retries for three frames. `src/ui/file_picker.h` writes the
same scroll fields to reveal a result. Chat history, asset browsers, and game
inventories also need positions to survive row rebuilds and UI scale changes.

`HasScrollView` already distinguishes an unmeasured viewport and provides
`anchor_scroll`. `ui/systems.h::apply_scroll_anchor` follows a rendered child
entity. That supports ordinary insertions above visible content but does not
supply a logical item key across replaced entities, pages, or document switches.

Extend scroll restoration with an opaque item key, a position within the item,
a viewport alignment fraction, and optional occlusion insets. The host resolves
the key to current geometry after the matching content and layout generation is
ready. A pending request needs explicit applied, unavailable, and cancelled
results. User scrolling cancels it. Updating the scroll target and easing state
should be one operation, without a fixed frame retry count.

Current workaround: app pixel maps and manual field updates. Status:
source-reviewed. This audit did not reproduce a zoom-restoration failure.
Acceptance should test delayed content, replaced entities, removed anchor rows,
resizing, zoom, and cancellation by real wheel input. The existing variable-height
virtual-list request remains separate from the anchor contract.

Step 37's long-Markdown test exposed an app capture-order defect: a large
wheel jump changed scroll offset after the virtual rows had been built. No new
visible anchor existed that frame, but the app marked the offset sampled anyway.
A later resize restored the previous bottom position. The app now retains the
pending offset until it can sample a visible row. This is an application fix,
not proof of a framework defect. A reusable post-layout anchor contract should
make the distinction between requested position and sampled position explicit.

### U8. Read-only selection over a virtualized text source

**2026-11-26 status:** `with_selectable_text` landed for ordinary labels, but
it deliberately does not cover selection across virtualized rows or a
source-backed diff. FloatingHotel's document-position selection stays.

At the audit baseline, `src/ui/diff_renderer.h::diff_sel` stored endpoints as
entity IDs and byte columns, rebuilt line records each frame, measured prefixes
for hit testing, and copied from `lastLines`. `ordered_span` could not resolve
an endpoint whose rendered row had disappeared. Log viewers, chat transcripts, and scripting
inspectors need selection independent of the rows currently rendered.

Afterhours already exposes backend-independent selection geometry in
`vendor/afterhours/src/plugins/ui/text_selection.h`, including `Selection`, `offset_nearest_x`,
`selection_rects`, and `substring`. Text input also has cursor and word movement
helpers. Those should be reused. The missing contract is a read-only text source
whose positions remain valid outside the current rendered line array.

A text-source adapter should provide stable positions, bounded range reads, and
source-to-display mappings for tabs and soft wraps. A selection controller can
then own the caret, mouse and keyboard movement, and edge autoscroll while a row
renderer supplies current geometry. Copy must preserve source whitespace and
line endings, report an exceeded output limit, and avoid silently truncating.
The host owns revision identity, diff sides, and optional location headers.

Current workaround: app selection records and copy assembly. Status:
source-reviewed limitation, not a newly reproduced selection failure. No native
selection test was run in this audit. Tests must select beyond the virtual
viewport, return to the starting row, and compare copied bytes with the original
source, including Unicode, tabs, and CRLF.

Step 48 follow-up: the old executable reproduced the missing-endpoint copy failure
in 18 scope/zoom cases. Selection now lives on the document as source positions,
with a bounded worker reading exact text for clipboard commands. All 18 final
hidden native cases, three edge-drag cases, cancellation, 8 MiB refusal, and nine
offscreen screenshot journeys pass; geometry checks cover clipped highlights and
split-side isolation. The implementation remains app-owned because Afterhours
has no source-backed selection controller. See `docs/reading-navigation-step48.md`
and the packaged step-48 evidence for the reproduced failure and final checks.

### U9. Optional text normalization at file and clipboard boundaries

**2026-11-26 status:** `sanitize_utf8` landed for ingest. It does not replace
FloatingHotel's decoder, which also detects/converts UTF-16, reports malformed
status and encoding, and identifies NUL/binary content.

`src/util/text_decode.h` converts UTF-16 to UTF-8, handles BOMs, replaces malformed
sequences, and reports encoding and NUL content. The source reader and
`src/util/file_page.h` need this before rendering arbitrary repository bytes.
Text asset importers and game log viewers need the same conversion without
embedding file-format policy in every widget.

The existing `ui/text_selection.h` and `ui/text_input/utils.h` provide UTF-8
character and cursor utilities. They are not a byte-stream decoder or a file
encoding detector. A small optional normalization utility should return valid
UTF-8, the selected encoding, and malformed-input status. Explicit encoding
selection should take precedence over BOM detection, with BOM behavior documented.
Heuristic detection should remain opt-in. Streaming callers need retained partial
code units across input chunks and an explicit finish operation.

Current workaround: app decoding and page collection. NUL-based binary detection
and its UI presentation remain host policy. Verification in this audit: all
eight `tests/unit/test_text_decode.cpp` cases passed after a fresh build,
including malformed UTF-8, unmatched UTF-16 surrogates, and an odd trailing byte.
The proposed streaming normalization API was not implemented or tested. This
request complements the existing diagnostic JSON replacement entry; neither
utility establishes Unicode grapheme selection or bidirectional text support.

### Audit verification and limits

Fresh builds used `nice -n 10 zig c++ -std=c++23 -O0 -I.` with each corresponding
`tests/unit/test_<name>.cpp`, then ran the resulting binaries with `nice -n 10`.
The 15 selected tests passed. Binaries remain under `/tmp/fh-afterhours-review`.
The logs are retained in `docs/reading-navigation-evidence` as `async_task.log`,
`byte_cache.log`, `token_cache.log`, and `text_decode.log`.

This audit changed only this document. No Afterhours source was edited or upstreamed.
UI, native lifecycle, and cross-platform acceptance work is stated separately
in each entry. Existing screenshot and regression results elsewhere in this
document remain historical evidence, not checks rerun by this audit.

### E2E asynchronous checkpoints need explicit retry and a wall-clock deadline

The first reading-journey probe returned while waiting for a rendered destination.
Afterhours treated that as an unknown command because the handler had neither
consumed the command nor called `PendingE2ECommand::retry`. Calling `retry` is
the supported API; the omission was an app test-adapter error.

The generic pending-command timeout counts 30 simulated ticks. Headless execution
can consume those ticks before a cold Git read completes. The reading probe now
uses `retry`, resets the frame counter while waiting, and fails after a 15-second
steady-clock deadline. An optional per-command wall-clock deadline would make
network, asset-loading, and editor tests easier to write without mutating the
framework's frame counter. The rejected run is retained at
`output/reading-navigation/probe/zoom-100-run-1/run.log`.

A second replay showed a separate ordering limitation. `E2ERunner::tick` drains
pending commands at script boundaries, but dispatches the next ordinary command
even while a previous handler retries. `retry` acknowledges a pending command;
it does not make that command a barrier. The app now pauses script dispatch while
a reading checkpoint is pending and continues update/render passes until it
completes or fails. The rejected evidence is in
`output/reading-navigation/probe-retry/zoom-100-run-1/run.log`. An explicit
blocking-command contract would help tests that must wait for a scene, asset,
network response, or rendered destination before issuing dependent input.

### Rendered registry entries can be completely clipped

The 200% source capture in
`output/reading-navigation/baseline/zoom-200-run-3/cold_source.json` includes line
12 with `was_rendered_to_screen` true, although the screenshot ends at line 10.
The computed clip rectangle excludes that row. That flag alone cannot prove
viewport visibility. This is a diagnostic contract limitation, not evidence that
the renderer painted outside its clip.

The app's baseline checker now requires a positive visible rectangle and a fully
visible first code line. Input timing also waits for a matching visible destination
heading. A framework visibility predicate that combines rendering, clipping, and
viewport intersection would help inventory, list, and editor tests avoid the same
false assertion. The app uses `ui::visible_rect` as its current workaround.

### Native-menu test mode depends on environment presence

The hidden native-menu probe is enabled when `FH_NATIVE_MENUS` exists, including the value `0`. Native tests must use `native_menu_action`; legacy tests that click the in-app View or Edit menus must remove this variable from their environment. The step-02 navigation replay initially used the wrong mode and could not find those menu targets. Evidence: `output/navigation-design/existing-navigation.log` and `existing-navigation-inapp.log`. This is an application test-runner configuration limitation, not a missing Afterhours menu widget.

Step 02 also found stale fixed mouse coordinates in the comment-jump replay: y=340–360 hit the file footer after header layout changes. The layout dump placed source lines 4 and 5 at y=276–300 and y=300–324; the test now drags through those rows. Semantic code-position input would make these tests less sensitive to unrelated chrome changes, alongside the text-viewer candidates above.

### Text assertions see styled runs separately

The commit-search replay displayed `tests/test_utils.cpp` correctly, but `expect_text` could only see the styled runs `tests/` and `test_utils.cpp`. The layout dump retained the complete label on `file_header_label`. Workaround: assert the visible filename and header entity, then check the combined label and geometry in the layout JSON. Evidence: `output/navigation-design/regressions-final/improvement_27_commit_search`. An element-scoped text assertion that uses the complete label would help other apps verify styled breadcrumbs and filenames without depending on color-run boundaries.

### Zoom-safe app pointer coordinates and text sizing

The command-log resize handle mixed `LayoutComponent` logical coordinates with
`graphics::get_mouse_position()` screen coordinates, then applied a second
screen-height conversion. At increased UI zoom this moves or clamps the divider
away from the pointer. The app now reads the injected/real UI pointer through
`ctx.mouse.pos`, divides by `ui::zoom::get()` once, and stores a logical height.
This is an app integration bug; no vendor changes were required.

A framework conversion API that distinguishes layout coordinates from rendered
coordinates would help other apps and games implement draggable panels. Keep
rendering, clipping, pointer hit testing, and test-driver targeting on the same
rectangle helpers. The app's `hover_ui` test command uses the visible rendered
rectangle, and layout captures now record the pointer and resolved hover target.
`tests/zoom_hover.py` exercises row labels, icons, counts, and tab close controls
at 100%, 140%, and 200% zoom, with pixel checks for the entire row highlight.

The framework has font-size tiers but no independent text-scale setting covering
both tier-based and explicit pixel fonts. A shared text scale applied before
measurement, wrapping, selection geometry, and rendering would be useful for
accessibility in native apps and games. This patch uses the existing code-font
preference for Cmd± and raises its default from 16 to 17.6; UI zoom remains a
separate menu/pinch operation. It does not add a partial framework text-scale
hook that changes drawing without changing measurements.

### Wrapped code needs source-position mappings

A styled label with `TextOverflow::Wrap` does not make a fixed-height code row
into a wrapped reader. This app also widened the scroll content to the longest
line, so wrapping never had a useful width constraint. The app now creates
measured visual fragments inside the viewport, retaining each fragment's source
byte offset, line number, and diff side. Continuation gutters are blank; copying
joins fragments without inserting source newlines. Full-line syntax tokens are
sliced for display, and fragment rows use the existing viewport culling.

A framework text-layout result exposing visual rows, decoded byte ranges,
advances, and hit testing would benefit log viewers, chat, terminals, editors,
and in-game consoles. The current workaround shares the existing diff-metrics
cache budget rather than introducing another retention budget. Source-position
selection that survives reflow and offscreen dragging remains in planned steps
13 and 48; the current reader clears a selection on reflow instead of allowing
its entity IDs to refer to different text.

Step 36's 256 KiB long-line fixture exposed an app-side cost in this workaround:
rebuilding the full display string and its prefix for every visible fragment.
Preparing display text once per source line and advancing byte/column offsets
across fragments reduced the 100% zoom rendering p99 from 106.16 to 5.49 ms on
that fixture. An upstream layout result should make these mappings reusable
across slices, rather than requiring consumers to derive each prefix again.
This was a host rendering bug, not evidence that Afterhours layout itself took
106 ms. The app keeps the existing token and wrapping cache budgets.

### Restore window dimensions before native creation

The application stored window-size fields but never updated them on normal
shutdown or applied them to native startup. The fix loads configuration before
creating the window and persists expanded/dock dimensions separately. This is
application lifecycle integration, not a missing native resize operation.
A reusable persisted-window-state helper should expose initial size and a
settled resize notification, without animating or briefly showing a default
size. Tests use an isolated configuration directory and actual application
restarts; no user settings are overwritten by the restart runner.

The wrap fixture includes Japanese text, combining accents, flags, and family
emoji. Captures show missing-glyph boxes for several characters in the bundled
monospace font. Wrapping and copying retain their bytes; glyph coverage is still
a renderer/font limitation. Native wrapping now uses CoreFoundation composed
character ranges, and selection uses the same boundaries. A shared text layout
with font fallback and grapheme mapping would remove this app-specific adapter.
Line-ending annotations move to a continuation row when they do not fit beside
the source text; exceptionally narrow cells use a compact label with the full
line-ending description in its tooltip.

The geometry-driven selection replay caught an application layout bug at 200%
zoom: selecting code increased a narrow file header from one action row to two,
moving the code under the pointer. Narrow headers now reserve their action
height before selection. The regression asserts unchanged header geometry and
performs forward and reverse drags using measured source positions. This is an
app layout fix; it does not require a framework change.

Tab close controls now separate the full-height click target from a centered
20-pixel hover treatment. The outer button explicitly overrides its hover
background with transparency; the inner visual only highlights inside its own
rectangle. This uses existing Afterhours primitives. A reusable control primitive
with separate visual and hit bounds would help compact toolbars and game HUDs.

A 4,000-line source-page replay measured 43.85 ms render p99 and no wrap-cache
reuse with a 1 MiB wrap allocation. The total metrics budget remains 5 MiB, now
split as 2 MiB for signatures and 3 MiB for wrap positions. A unit test requires
all 4,000 lines to reuse their measurements; the native test also checks bounded
rendered rows and the existing 20 ms p99 gate. This is a measured redistribution
of existing retention, not an added cache budget.

`Padding{}` means unspecified padding for an Afterhours button, so it activates
the default button padding. A compact transparent close target must explicitly
set all four sides to `pixels(0)`. The first compact-close pixel check caught the
implicit padding shifting the glyph beyond the hit target; the corrected test
asserts matching centers, transparent idle edges, and a smaller hover rectangle.

After redistributing the metrics cache, the same large-page replay improved to
9.73 ms average but still missed the render gate at 28.26 ms p99. The renderer
was measuring each distinct line-number gutter even for offscreen rows. The
framework text cache has 4,096 entries, close to a full source page before
ordinary labels are counted. The app now measures equal-length spaces for its
monospace gutters. A reusable monospace cell-advance measurement would help
large tables, terminals, logs, and game consoles avoid this cache pressure.

The gutter change passed the same native replay at 4.62 ms average and 6.46 ms
p99, with 172 rendered entities and unchanged cache limits. Evidence:
`output/priority-polish/large-build9/run.log`.

### Preview navigation and focus

Resetting `UIContext::focus_id` to `ROOT` does not leave focus empty. The next
focusable control calls `try_to_grab` and takes it. A native Enter-to-keep test
caught focus moving to an unrelated control after document navigation. The app
now retains a repository-owned destination DocumentId and resolves its semantic
focus target after layout and modal input-gate removal. A line-navigation replay
caught `set_focus` rejecting the new tab while the closing picker's input gate
was still installed; consuming the request at that point left focus on the
repository tab. The request now survives until the final focus phase. A reusable semantic focus request, resolved
after rebuilding UI entities and scoped to its owning screen, would help menus,
document readers, inventory screens, and game overlays.

The general button API exposes click activation but no reusable double-click
sequence. Text inputs implement their own click counts. Preview tabs use an
app-owned 500 ms sequence keyed by repository, control region, and destination;
file rows include their file/line target, while tabs ignore reading-position
changes. A reusable click-count gesture with target identity, pointer-distance
limits, and cancellation on drag or navigation would help document tabs, asset
browsers, inventories, and map interactions.

### Label fit in layout probes

A tab can fit inside its parent while its text is still ellipsized. The first
step 05 replay passed rectangle visibility checks but screenshots showed clipped
parent-path labels. The app now records measured text width at the resolved font
size in layout dumps and compares it with the label rectangle and text inset.
A framework probe exposing resolved label bounds, text width, and whether
ellipsis was applied would help automated checks for tabs, menus, inventory
labels, and localized game interfaces. The app also explicitly reserves the
icon, gaps, close target, and badge before allocating its title width.

### Horizontal tab scrolling and bounded menus

Directly assigning `HasScrollView::scroll_offset` during UI construction can be
undone before input easing: layout calls `clamp_scroll`, which updates
`last_eased_offset`. The step 06 native replay caught arrow-button scrolling
snapping back. The app assigns both `scroll_offset` and `scroll_target` for
explicit jumps. A framework `scroll_to` operation would remove this ordering
dependency for tab strips, carousels, lists, and game inventories.

The app's existing context menu assumed every row fit in the window. Open-tabs
menus require a bounded viewport, wheel scrolling, and keyboard reveal. The
app now renders only fully visible rows and blocks underlying scroll views
while a context menu owns input. A framework menu/list primitive with scrolling,
selection reveal, and input ownership would be useful beyond this application.

The configured text font did not render the dropdown triangle or current-tab
checkmark used by the first open-tabs menu. Geometry checks still saw labels,
so the native replay now checks icon pixels. The app uses its existing vector
chevron and a textual Current label. Also, a button's default hover background
can override a manually chosen menu-selection background; the menu now sets
both explicitly so keyboard selection has one visible highlight.

### Reading positions and reused UI entities

A scroll entity from the previous frame may already represent the next document
when the app saves the old document's reading position. The close/reopen replay
caught a saved offset being replaced with zero through that reused entity. The
app now saves the last observed offset associated with the old destination.
A framework scroll snapshot keyed by stable content identity, with a layout-ready
restore operation, would help readers, inspectors, inventory lists, and game
menus preserve position across immediate-mode rebuilds. Logical-anchor restore
is still pending in step 13; this fix does not claim to provide it.

### Reordering collections with pointer capture

Afterhours already retains the active pointer target across frames and provides
press/release click modes. Document tabs compose those pieces into an app-owned
drag: threshold, stable item identity, insertion marker, bounded edge scrolling,
Escape/outside/resize cancellation, and suppression of nested close actions.
A reusable reorderable collection gesture could support asset lists, game
inventories, loadout slots, and document strips. It should report a move intent
without changing selection or content loading, and let the caller own ordering.
The native replay in `tests/reorder_tabs.py` checks these interactions at three
zooms, including dragging an inactive tab without changing the reader.

### Modifier-held navigation overlays

The framework test driver's `key` command releases its modifier after two
frames. That cannot observe a recent-tab switcher while Ctrl remains held over
several Tab presses. The app adds `hold_key` and `release_key` commands using the
existing key injector. Reusable held-modifier commands would also help test game
radial menus, temporary scoreboards, alternate tool modes, and chorded controls.
The app's switcher retains a fixed candidate order during the chord, removes
closed candidates, and commits only on modifier release.

### Restoring virtualized reading anchors after layout readiness

The app records path, revision, side, source line, decoded character column, and
viewport fraction from measured rows after layout. It restores those values only
after matching content and initial refresh are ready. An explicit layout-ready restoration hook would help
readers, editors, inventories, and inspectors retain a logical item through
recreated entities and changed row heights. It should support caller-owned
identity, cancellation, and one-shot application without a fixed frame retry
count. The session replay in `tests/restore_tabs.py` covers source and review
restoration. Step 13 uses an ordinary ECS system immediately after
`registerUIPostLayoutSystems` to resolve anchors against current row bounds.
`tests/anchor_layout.py` covers zoom, resize, folding, Markdown reflow, and
manual scrolling; `tests/anchor_pages.py` covers a released source page.
Step 30 also exposed an app-side boundary error: two wrapped fragments could
claim the same column, and the next fragment might not be in the virtualized
row registry yet. Non-final fragments now exclude their end column; the final
fragment accepts the end-of-line position. An upstream text-position projection
API should define fragment-end affinity explicitly so editors and chat readers
can restore positions without depending on which rows have been rendered.
`tests/history_visits.py` covers the UTF-8 boundary and closed-document revisit.

### Scroll anchoring needs an application-owned identity option

`HasScrollView` automatically pins a child entity ID. Virtualized readers can
reuse that entity for a different row or spacer after the visible range changes.
This conflicts with application-owned logical anchors. The reader clears
`anchor_child` before each layout and owns restoration with source positions.
The step 13 source-origin replay also caught a separate six-line drift.
The app had clamped virtualized restoration against the previous view's empty
content size, then accepted a nearby rendered line before building the target.
It now chooses the virtual range without that stale bound and clamps only after
layout measures the current content. A nearest-line fallback must match the
nearest line in the document, not merely the last rendered row. An explicit opt-out or
a caller-supplied stable item key would help code readers, game inventories,
chat logs, and recycled lists. Preserve automatic child anchoring as the default
for ordinary lists. Evidence: `output/step13-source_origin/100/journey` and
`tests/source_origin.py`.

### Composite text inputs need a focus operation on the returned widget

`text_input()` returns an outer entity containing `HasTextInputState`. Its
focusable field is a child entity. Calling `ctx.set_focus(result.ent().id)`
focuses the wrapper, then `EndUIContextManager` drops that focus because the
wrapper is not in the focusable set. The first focusable repository tab then
takes focus. This was reproduced when opening Quick Open and Find in
`output/step14-focus-first`.

The app's `ui::focus_control` resolves a text input to its focusable child. Its
semantic focus identity remains the named outer input, so a rebuilt field can
receive focus without retaining the old child's entity ID. A widget-level
focus operation or declared focus proxy would also help forms, game console
inputs, chat boxes, and reusable composite controls. The framework already has
focus clusters for drawing an outline; those do not make the wrapper a valid
keyboard-input target.

### Unconsumed mapped actions can reach a later control

`UIContext::last_action` is replaced when another mapped action arrives, but is
not cleared on an empty input frame. The app dismissed a picker with the raw
Escape key without consuming `MenuBack`. A later Find field consumed that stale
action and blurred. Returning to a tree filter could similarly lose focus after
the return was applied. `output/step14-focus-second` reproduces both paths.

The app now consumes `MenuBack` when its own dismissal handler handles Escape.
A frame-scoped action API, or an explicit distinction between queued commands
and current-frame key presses, would help games and apps that combine custom
shortcuts with text fields and framework controls. Tests should open a new
control after an earlier control handled a key and verify that the new control
does not receive the old action.

### Shortcut ownership must include composite text fields

A focused text-input entity is the inner field; its editing state belongs to
an ancestor. Text areas use a separate ECS component even though their C++
state inherits the single-line input state. Checking only the focused entity
for `HasTextInputState` let Option+Left navigate away from Quick Open while
its query was being edited. `output/step15-baseline` records the reproduction.

The app resolves semantic focus and checks both text component types along the
parent chain before dispatching reader shortcuts. Picker arrows and Enter,
Find Enter, and search submission are scoped to their own inputs. A reusable
command-routing API with editing, directional-input, and modal ownership would
help game consoles, tools, and apps avoid dispatching the same input twice.

### Windowless clipboard verification needs an injectable backend

The offscreen Metal runner does not initialize `sokol_app`, so the clipboard
plugin's `sapp_set_clipboard_string` and `sapp_get_clipboard_string` do not
provide a test clipboard. Copy assertions failed for both single-line and
multiline inputs in `output/step15-shortcuts-first`, while select-all followed
by replacement worked. The focus replay verifies selection replacement and
review isolation; it does not claim to verify system clipboard contents.
A scoped clipboard backend for tests would let apps and games exercise these
paths without changing the user's pasteboard or presenting a native window.

Step 47 uses a hidden native-window replay (`FH_TEST_NATIVE_HIDDEN` with
`--test-mode`) to initialize the actual Sokol clipboard. The test keeps window
presentation and activation disabled while its native draw timer runs.
`tests/clipboard_guard.swift` saves pasteboard items in memory, reads only text
containing the fixture marker, and restores the prior items if the marker is
still present. It does not log unrelated clipboard contents. This workaround
uses the general pasteboard during the test; concurrent user copying is not
isolated. A named pasteboard or injectable scoped clipboard remains the useful
upstream interface for apps, game consoles, and automated UI tests.

Step 48 replaces the general-pasteboard guard with a named pasteboard per
native replay. Hidden test mode redirects the process-local AppKit lookup;
the helper reads and releases only that board. This avoids altering the user's
clipboard and makes cancellation failures reproducible. The renderer still
needs separate offscreen screenshots. A scoped clipboard interface upstream
would remove this AppKit-specific test adapter.

### Modal centering mixes physical resolution and scaled pixels

`modal::detail::modal_impl` centers its container using the physical
`ProvidesCurrentResolution` dimensions, then passes the position through
`with_absolute_position`, which applies UI scaling. At 200% zoom, the shortcut
dialog's left edge was at 1180 on an 1800-pixel viewport and its Close button
was offscreen. `output/step16-escape-first/200/shortcuts.png` records this.

The app sizes this dialog from the logical viewport and assigns its final
absolute position in physical pixels after creation. Its backdrop uses the
logical viewport dimensions. Geometry assertions cover the dialog bounds and
Close button at 100%, 140%, and 200%. A framework modal should use the layout
viewport's coordinate space consistently for size, centering, clipping, and
hit testing. This matters to settings dialogs and game pause menus as well.

### Escape dismissal needs one owner across temporary controls

Independent raw Escape handlers can dismiss a popup and navigate the document
underneath on the same key press. The app uses its semantic focus return stack
to choose one dismissal, with menus above other panels. With no temporary UI,
Escape leaves the document unchanged. A framework dismissal stack shared by
menus, pickers, overlays, and dialogs would remove this coordination from apps.

### Virtual list tab order follows creation order instead of visual order

In the tree-state replay, Tab from the file filter moved to the sidebar
splitter, reader, zoom controls, and history before reaching the first file
row on the seventh press. `output/step17-tab-probe` captures every focus target.
The rows are focusable and their existing focus outlines render correctly;
the traversal order reflects when pooled UI entities entered the collection.

Step 18 supplies a path-based tree focus group, directs Tab from the filter
to its remembered row, and handles arrows before generic traversal. It guides
the virtual list to an unrendered destination before building rows, then
reveals the focused row through its scroll ancestors after layout. A framework
focus group with explicit visual order and one Tab entry point would help
virtualized inventories, menus, file browsers, and game editors. It should
retain focus by item identity when pooled entities are reused.

### E2E character injection needs Unicode code points

The framework's E2E key queue stores `char_value` as `char`, and
`test_input::get_char_pressed` returns that byte as an integer. Injecting
`é` with `type` therefore yields UTF-8 bytes instead of the Unicode code
point expected by the native input API; signed bytes are negative.
`output/step19-type-second/100/unicode.json` records a failed type-to-select
attempt despite the correctly decoded filename being present.

The app does not change production character decoding to fit the test queue.
Its pure selector tests Unicode prefixes, and the native replay types an
ASCII prefix to select a filename containing Unicode. A code-point E2E
queue would let apps and games test international text input using the same
contract as the native backend. Unicode prefix injection remains unverified
in the current native runner.

### Virtual-list reveal needs the actual list handle

`imm::mk(parent, index)` includes its source location in widget identity. Two
calls with the same parent and index on different lines create different
widgets. A pre-layout reveal helper was therefore looking at a new entity
without `HasScrollView`, while the real list stayed at its old offset.
Incremental arrows could succeed through overscan; a direct jump to a distant
row exposed the error in `output/step20-reveal-probe`.

The app now creates one `EntityParent` handle and passes it to both reveal and
virtual-list construction. A framework `scroll_to_item` operation that applies
before windowing would spare inventory, file-browser, and editor callers this
coordination and make offscreen keyboard destinations easier to test.

Reusing the virtual list's entity for a plain empty panel also preserves
`unbuilt_content_size`, leaving a phantom scrollbar. The app keeps a separate
empty-panel identity. A framework virtual-list empty state should reset this
size while preserving the logical list's position for returning results.
The reproduction is `output/step20-reveal-handle/100/empty_filter.json`.

### Keyed virtual lists should preserve viewport and focus through updates

The app's virtual-list wrapper receives row indices. Its file lists therefore
map the visible path, fractional row offset, and keyboard-focus path into each
new row sequence before windowing. A removed row uses the nearest surviving
row in the old sequence; an empty filter retains the anchor for later results.
Manual scrolling cancels a retained position that was clamped by a short list.

A framework list API with stable item keys could own this bookkeeping for
inventory lists, quest logs, leaderboards, and file browsers. The app workaround
is `ui::replace_tree_rows` in `src/ui/tree_keyboard.h`; the native replay is
`tests/tree_refresh_position.py`. This is an integration capability request,
not a proposed change to the current cache limits.

The step 24 reveal regression also distinguished a focused list item from a
focused control in the broader Tree region. Restoring the latter moved focus
from the Review button into a file row. The app now requires the focus target
to belong to the previous row sequence. Reproduction:
`output/step24-final-tree_reveal/100/back_review.json`.

### Text fields should own directional focus routing

A single-line text field's focused child lacks `ConsumesDirectionalInput`.
`ComponentConfig::inherit_from` does not propagate that flag, and the focus
router checks only the focused entity. Down Arrow in the review file filter
therefore left editing and selected a file row. The native history-keyboard
replay reproduced this at `output/step25-keyboard/100/editing.json`.

`BeginFocusFrame` now marks the focused entity when its semantic owner is a
text control. This keeps arrows with the editor while preserving Tab traversal.
Afterhours text-input and text-area widgets should declare this ownership on
their focusable child. Search boxes, chat inputs, game-console commands, and
forms would benefit without each app adapting the composite's internal focus.

### Virtual lists should expose their visible range for incremental loading

`virtual_list_impl` computes the viewport range and adds four overscan rows,
but its public result only returns the list entity. History pagination currently
requests the next page from the final row's render callback, guarded by one
repository-owned request. That works, but ties loading to rendering and implicit
overscan behavior.

An exposed visible/overscan range or an explicit near-end notification would
serve chat history, activity feeds, paged inventories, and leaderboards. Keep
fetching and retry state in the app; expose list geometry without requiring a
render callback to schedule work. `tests/history_pagination.py` verifies that
appending pages preserves existing rows at three zoom levels.

A focused pagination/retry row disappearing after append also lets framework
focus fall back to a repository tab (`output/step26-verified-history_pagination/140/appended.json`).
The sidebar restores the selected commit's focus before rebuilding the list.
An explicit focus fallback for removed list items would avoid this application
coordination in any progressively loaded list.

### Scroll input and scrollbar dragging should respect modal input gates

`HandleScrollInput` and `HandleScrollbarDrag` check viewport geometry but do
not call `UIContext::is_input_allowed`. A modal's pointer gate therefore does
not protect the scroll views behind it. The app's existing clipped-scroll
adapter now checks gates before scrolling, and a scrollbar adapter does the
same for dragging. Blocked views stop at their current offset.

The modal gate is normally registered after the app's UI post-layout systems.
Quick Open installs its own descendant gate before those systems run. A single
framework input boundary, applied before hit testing, scroll, and drag handling,
would help dialogs, command palettes, inventory popovers, and game pause menus.
`tests/quick_open_overlay.py` exercises background wheel input and outside clicks.

The picker also reuses the existing zoom-corrected modal positioning workaround.
Native modal focus restoration stores entity IDs; the app disables that return
path and uses its semantic focus stack so closed documents and other repositories
cannot regain stale focus.

### Virtualized controls need a semantic focus fallback

Step 43's 200% native replay expanded context above a hunk while preserving the
reading line. The invoking hunk header left the rendered region, and framework
focus fell back to a repository tab. The code viewer then lost its reading
shortcuts. Evidence: `output/step43-final/context/200/above.json` and
`docs/reading-navigation-evidence/step43`.

When context is applied, the app transfers existing code focus to the stable
Code region before rebuilding virtual rows. It leaves text inputs and other
regions alone. Afterhours could support a declared fallback focus region for
removed or virtualized controls; this would help editors, scrolling inventories,
and game lists keep keyboard ownership through asynchronous layout changes.

### Click-to-focus containers need independent hover styling

Adding `HasClickListener` makes an entity eligible for the renderer's default
hover tint. The app uses this component to focus large Code, Tree, and History
regions when their empty space is clicked. Those regions consequently turned
whole sidebar and reader backgrounds gray on hover, including an empty staged
review. This reproduced the user's September 13 screenshots.

`bind_focus_region` now sets `HasColor::skip_hover_override` on these region containers.
Ordinary control binding only assigns its semantic focus identity, so buttons
such as history Retry retain their hover treatment.
Rows and buttons keep their own hover treatments. A framework focusable-region
primitive should separate pointer focus from button feedback, so editors,
canvases, inventories, and game panels can accept focus without looking pressed.
`tests/container_hover.py` compares pixels in empty and populated staged and
unstaged views at 100%, 140%, and 200% zoom; the existing zoom-hover replay checks
that file rows and compact close buttons still respond at the correct location.

### Native resize invalidation needs a drawable before returning to AppKit

Sokol's macOS `windowDidResize:` updates the drawable dimensions but does not
request a draw. A hidden native probe pauses periodic drawing and changes
`NSWindow` sizes inside `NSEventTrackingRunLoopMode`. Before the app workaround,
the setter returned without another frame (`output/live-resize-baseline2.log`).
The app's delegate adapter forwards dimension updates, then draws directly,
except during a queued programmatic resize or recursive resize draw. Window
and layer backgrounds are dark before presentation and after resize.

Afterhours/Sokol should expose resize invalidation and presentation as one
backend operation. Desktop tools, editors, and windowed games need this even
when normal frame pacing is idle or the run loop is tracking a window edge.
The native probe checks matching drawable dimensions and reads rendered pixels
back from the GPU. It exercises native window callbacks in the tracking loop;
it does not simulate a physical mouse drag or measure the compositor's display.
The probe passed at native and Retina scale with Metal validation enabled.
All 16 sizes rendered matching pixels before the setter returned. Draw callbacks
took 1.5–7.6 ms; the first two Retina window operations took 21.6 and 33.6 ms,
so native-window overhead still exceeds 16 ms in those cold cases. Evidence is
in `docs/reading-navigation-evidence/step27/native-resize`.

Quick Open also switched theme font tiers to explicit logical pixel sizes.
`with_font_size(FontSize)` resolves through `h720`, which produced tiny labels
inside a zoomed modal. The app's existing button presets use `pixels` for this
reason. A documented, consistently zoomed typography scale would avoid mixing
viewport-relative font tiers with logical-pixel controls.

### A post-build phase for actions that replace rendered models

The step 31 comparison-source replay found an app lifetime bug: Open file
released the comparison vector while `render_diff` was still iterating it.
The app now returns immediately after navigation. The debugger backtrace is
in `docs/reading-navigation-evidence/step31/failures/comparison-open`.

A supported post-build UI action phase would help scene switches in games and
document switches in apps avoid this pattern. Actions should own their input
values and identify their repository, document, or scene owner so stale actions
can be discarded. Current button handling returns activation during the
caller's render pass; the app keeps navigation explicit and ends that pass
after a model-replacing action. No additional content retention is needed.

### Search disclosure controls need drawn chevrons

The step 33 native search capture contained `▸`/`▾` in layout text but no visible
triangle in the default font. Floatinghotel uses its existing `chrome_icon`
chevrons instead. This is another concrete use for a framework-owned disclosure
icon/control with reliable sizing, hit targeting, focus, and expanded state.
File trees, inspector sections, inventory categories, and scene outliners need
the same control. Evidence: `docs/reading-navigation-evidence/step33/failures`.

### Search snippets expose missing font fallback and styled-label padding

The step 33 `long_match` capture preserves `日本` in layout text and correctly
highlights the surrounding ASCII matches, but the bundled Roboto face does not
draw those Japanese glyphs. `src/preload.cpp` loads Roboto and JetBrains Mono;
`TextSpan` owns text, color, and weight without a font fallback field. Search
highlight unit tests verify UTF-8 offsets; they do not establish glyph coverage.
A backend font fallback chain with consistent measurement, selection geometry,
and bounded glyph caching would help multilingual app labels and game dialogue.
This remains an open visual limitation; no replacement framework was introduced.

Adding left padding to a styled search-header button did not reserve the intended
space before its text in the captured native output. An absolutely placed
chevron overlapped the label. The app now uses a row with separate icon and label
children and checks that their rectangles do not overlap. Upstream styled-label
padding/measurement parity would remove this workaround.

### Worker tests need wall-clock condition waits

A fixed `wait_frames 80` in the cancellation replay could finish before a Git
worker started. `PendingE2ECommand` also times custom commands out after 30 ticks,
which is much shorter than 0.5 seconds under the accelerated runner. The app's
`wait_for_path` probe retries until a worker-owned marker exists, with a ten-second
steady-clock deadline, resetting the framework tick counter while pending.
The app also pauses command dispatch through its existing checkpoint gate:
`retry()` alone leaves a command pending but does not stop subsequent commands.
A generic deadline-based condition wait would help asynchronous asset-loading,
networking, and document tests without fixed frame guesses or blocking sleeps.

### Styled labels split the native text assertion registry

The search-filter replay's `expect_text "changed needle"` stopped matching after
highlighting split the label into `changed ` and `needle` spans. The full joined
label is present in the layout dump and remains clickable, but the native visible
text registry records spans separately. The app tests now assert file-group
identities and the opened source line. Upstream text assertions should also expose
the joined visible label, so adding color does not change its text identity.

### Text-input focus belongs to an inner field

Step 34's search query wrapper was returned by `text_input`, while focus belonged
to its `text_input_field` child. Comparing the returned entity ID to `focus_id`
therefore skipped keyboard preview commands. Floatinghotel routes through its
semantic `FocusIdentity` ancestor instead. A widget-owned focus query or semantic
control handle would help search boxes, game consoles, and composite controls
without exposing their internal entity structure.

### Semantic focus restoration must resolve an eligible control

Step 35 exposed an unnamed layout child inheriting the same semantic Code region
as its viewport. Restoring an empty control identity selected that child, then
Afterhours dropped focus because the child had no input listener. The app now
requires a click listener or text control for exact restoration, and a region's
own click listener for fallback. Reading viewports also have an explicit semantic
item so empty hunk headers cannot substitute for the code viewport. A semantic focus-region API should distinguish
an ancestor's identity from the region's eligible focus destination. Document
viewers, game inventories, and composite inspectors would benefit from that
contract without adding click handlers to decorative children.

### Scroll content needs a reserved scrollbar gutter

Step 39's Details control fit its parent geometry but its right focus border
overlapped the scrollbar. The app reserves eight logical pixels in the metadata
row. A scroll-container content-inset API would let inspectors, inventories, and
readers reserve that space consistently without each child knowing scrollbar
width. The expanded metadata card's oversized corners were an app configuration
omission, fixed with the existing explicit corner-radius API. Evidence is in
`docs/reading-navigation-evidence/step39`.

Step 41 applies the same scrollbar gutter to hover-revealed hunk actions.
Afterhours already supports inherited opacity; the app uses it to reserve
action space while hiding the controls. The native layout dump now includes
effective opacity so behavioral checks can distinguish layout presence from
visibility. Compact icon buttons also need explicit child alignment and enough
content height after padding; those were app styling corrections.

### Layout snapshots retain geometry for inactive entities

The step 44 caret replay initially counted a previously rendered caret whose
`visible_rect` still had nonzero dimensions. The entity was correctly marked
`rendered=false`; the test now requires both rendered state and clipped geometry.
An explicit effective-visibility field or shared snapshot query would help
native UI tests for virtualized readers, inventories, and transient overlays
avoid interpreting retained layout data as a visible element. This was a test
assumption, not a caret left on screen. Evidence is in
`docs/reading-navigation-evidence/step44/first-native.tar.gz`.

### Read-only text views need shared selection gestures and word policy

Step 45 uses the existing zoom-aware pointer coordinates but implements click
counting in the reader. Afterhours text inputs have their own 0.4-second
multi-click detection inside the editable widgets. A reusable gesture result
(single, double, triple, Shift extension), with a distance threshold in screen
coordinates, would help code viewers, log panes, terminals, and game chat logs.
It should preserve pointer capture and input gating without requiring an editable
text field.

The framework's `select_word_at` treats underscore as punctuation. Code selection
needs identifier words such as `alpha_éλ`; the app adds a small word-boundary helper
using decoded composed-character boundaries. An upstream configurable word policy
would let code viewers and ordinary prose fields share geometry and gesture
handling without forcing the same definition of a word. Step 45's native replay
checks exact UTF-8/tab bytes and both split-diff sides.

### Raw shortcut handlers must consume the matching UI action

The reader handled the physical Cmd+C key while `UIContext::last_action` still
contained `TextCopy`. Opening Quick Open later let its newly focused text input
consume that old action and replace the clipboard with the selected filename.
The native cancellation replay captured `sentinel.txt` on the private pasteboard
instead of the previously copied source text. This reproduced after isolating
the pasteboard, so it was not external clipboard interference.

The app now consumes `TextCopy` at the reader focus boundary. Apps and games
mixing direct key handlers with framework widgets need a single dispatch/consume
contract, or a frame-scoped action lifetime, so an action cannot run again after
focus changes. Tests should copy in a custom viewer, open a text field, and
verify that merely opening the field leaves the clipboard unchanged.

## Avoid duplicate key ownership in bounded content caches

Step 48's 5,000-line diff produced 10,000 display rows and repeatedly evicted its
own wrapping results from the 3 MiB cache. A stack sample from the real offscreen renderer attributed most content
preparation time to recomputing Unicode cluster boundaries; the native replay
measured p99 203.60 ms. Each cache key was owned both by its LRU entry and by its
lookup table. The repository cache now keeps one stable key in the list and a
string view in the table, removes the view before freeing the entry, and trims
excess key capacity. The same fixture fits without changing the budget. Tests
cover a complete second traversal, rehashing, replacement, and eviction, including
an AddressSanitizer/UndefinedBehaviorSanitizer run.

A reusable byte-budgeted LRU with explicit ownership accounting would help games
cache text layout, sprites, and resource metadata without duplicating large keys.
Keep this independent of floatinghotel's document model. The first 100% unified replay improved to p99 17.75 ms, but 200% split still
failed at 194.85 ms because narrow columns retain more offsets. The follow-up
uses published file/side/line identities instead of full text keys and trims
offset-vector capacity. A 10,000-row, eight-column unit replay passes without
increasing the budget. The final 18-case native replay passes at p99 3.39–17.50 ms, with a 23.84 ms
maximum frame. Failed and passing results are retained; evidence starts at `output/step48-profile/sample.txt` and
`output/step48-final/100-unified/journey.log`.

## Incremental text windows need a layout acknowledgement

Step 49 keeps up to three adjacent source pages in one active byte window. A fast
read initially restored the pre-scroll anchor because the next page completed
before layout recorded the new scroll position. The host now keeps the existing
reader active while that read is pending and waits for matching laid-out scroll
state before publishing the joined window. This uses existing repository-owned
reading state; it does not add frame-count retries or a retention cache.

Afterhours could expose a layout-generation acknowledgement with a stable logical
anchor for virtualized text, logs, and inventories that prepend or evict rows.
Applications would supply source positions and bounded data; the framework would
identify which layout and viewport produced an anchor. The first two failed
native runs are `output/step49-first` and `output/step49-second`. Final source
and fragment acceptance passes; see `docs/reading-navigation-step49.md`.

## Accumulate virtualized extents without float32 drift

With 8,192 rows at 25.6 pixels, float32 spacer accumulation changes the total by
about 21 pixels when visible rows split the list into different groups. The
source reader interpreted that height change as a reflow and restored an old
anchor after ordinary scrolling. The app now accumulates diff extents with double
precision and recognizes source reflow from content identity, font/whitespace
mode, and viewport geometry. A source window's rounded aggregate height no longer
triggers restoration on its own.

The native layout still uses float coordinates. A reusable virtual-list geometry
helper should keep cumulative extents at higher precision until producing the
visible coordinates, and distinguish content changes from virtualization-only
regrouping. This would help long logs, asset browsers, inventories, and chat
history. Numeric evidence is `output/step49-height-rounding.json`. Final source-window
acceptance and geometry checks pass at all three zooms.

## Hot cache lookups should avoid formatting allocations

A step 49 native profile of a three-page reader put 1,037 of 1,738 sampled
hunk-rendering stacks under wrapping lookup. Formatting the same floating
width/font fields as decimal strings and concatenating key pieces allocated on
every line, even when wrapping was cached. The app now uses fixed-width binary
fields plus the source identity, preserving separate whitespace and content-key
modes. The existing budget remains unchanged. A reusable text-layout cache should
accept typed metric keys directly; games with long logs and virtualized asset
lists would avoid the same per-frame serialization cost. The measured failure
and profile are retained in `output/step49-continuous_source-final3` and
`output/step49-profile`. Final native source and fragment gates pass at p99
14.89 ms and 13.75 ms respectively; earlier misses remain in the evidence.

The same profile also exposed per-frame work proportional to all loaded source
lines, even when most rows were virtualized. The app now caches cumulative
wrapped-row counts under the existing metrics budget and skips offscreen source
lines without gutter preparation or wrap lookups. A shared variable-height list
index should expose cumulative extents and stable row identities, with explicit
invalidation for content and text metrics. This applies to wrapped chat, log,
and inventory rows as well as source code. Logical anchor and Find targets must
still be evaluated when outside the current viewport.

The long-line replay also found full display-text conversion before every token
cache hit and review-hunk hashing for a source-only view. The app now looks up
tokens by published file/side/line identity plus language and whitespace mode,
and builds display text only on a miss. Source-only hunks skip review-key hashing.
Framework text caches should accept immutable content identities and defer text
transformation until a miss. This matters for long console lines and game logs,
where a cache hit can otherwise still scan and allocate the entire line.
Evidence: `output/step49-fragment-profile/sample.log`.

The legacy EOF probe also treated a 0.00625-pixel layout rounding difference as
an invisible final row. The screenshot reports line 15,000 at y=848.40625 with
height 25.60000038 inside a viewport ending at y=874. The rendered clipped height
is 25.59375. That test now asserts the actual final-row text and geometry with a
0.5-pixel tolerance. Shared geometry assertions should distinguish subpixel
rounding from clipped content. Evidence is in `output/step49-legacy-failures`.

## State carried between chunks of styled text

Step 50 found that caching tokens by line text and language alone gives the wrong
colors when identical text follows different multiline comment or string prefixes.
The app now stores lexical state in bounded page cursors and includes incoming
state in token-cache keys. Diff sides carry independent states, including renamed
paths. The original color failure is recorded at all three zooms in
`output/step50-baseline`.

An upstream text-cache API could accept a small caller-owned incoming state and
return the outgoing state alongside spans. The framework need not own language
parsers. This would also support streaming console markup, dialogue formatting,
and shader/source previews in games without reprocessing entire documents after
each page load. Delimiter lookahead and source positions must survive chunk cuts;
keys must encode state fields rather than struct padding. The app's current
workaround keeps the existing cache budgets and native styled-text renderer.

## Folded ranges in a virtualized reader

Step 51 adds source folds using repository-owned line ranges. The renderer keeps
its cumulative index of original wrapped rows and skips folded intervals while
reserving the same row identities. Source selection, syntax, and copy positions
remain independent of the rendered entities. Gutter controls reserve two text
columns and use the same measured font/zoom geometry as the code.

A reusable Afterhours range-visibility adapter could provide this behavior for
source previews, nested game logs, stack traces, and inspectors. Applications
would supply proven ranges and stable source positions; the widget would handle
virtualization, disclosure targets, and anchor preservation. Language parsing
and repository identities should stay with the application.

The app also caught a CRLF boundary error in its first folding scanner: treating
CR and LF separately could expose braces inside a continued quoted string. It
now shares the lexer's line-lookahead helper. The failing test is retained in
`output/step51-crlf-before.log`. A framework callback for stateful text transforms
should carry logical line endings consistently for styling and range discovery.

The step 51 fast fold/close/reopen replay also exposed overlapping input ownership:
a child disclosure click entered the parent reader's text-selection gesture,
including edge autoscroll. The app excludes its two-column disclosure gutter
from text selection. Upstream, custom pointer gestures need an input-consumption
contract that composes with child buttons, including press/drag/release ownership.
This applies to tree inspectors, game editors, timelines, and selectable logs.
Fold generations also participate in the app's layout acknowledgement stamp so
geometry built before a visibility change cannot consume its new reading anchor.

### Delayed busy feedback without delaying destination identity

Step 53 keeps the destination heading immediate and delays busy text by 150 ms
from dispatch. The app owns the request identity and publication checks. A small
Afterhours delayed-visibility primitive, accepting a monotonic clock for tests,
would be useful for editor panels, asset browsers, save operations, and game
matchmaking menus. It must reset on replacement and disappear on completion;
it must not debounce the user's selection or defer the heading update.

### Cancellable speculative work with one admission slot

Step 54 uses a 150 ms hover intent and one background prefetch slot. Cancelling a
queued future does not immediately remove its executor job; dropping a tab must
not make that slot appear free prematurely. The app holds an admission permit
until the captured worker closure is destroyed and keeps completed content only
in its existing bounded cache.

A reusable Afterhours speculative-work slot should expose stable intent,
cancellation, queue admission, and foreground priority without storing renderer
payloads. Asset thumbnails, inventory previews, scene browsers, and inspector
panels could use it. Repository identity and Git revision validation remain
application policy. The app also switches intent to keyboard navigation until
the pointer moves, so a stationary pointer cannot keep warming the old row.

The step 54 native replay also needed the app's asynchronous frame barrier for
its prefetch wait. Retrying an ECS test command alone allowed the accelerated
command loop to advance without the intended rendered-frame handoff. The wait
now participates in the same host barrier as reading and filesystem waits,
with an explicit ten-second failure. A library-level await-condition command
should make this scheduling requirement explicit and reusable.

### Native dock verification and pending geometry (step 55)

The generic E2E resize command controls the test viewport; app-driven native
resizing is normally disabled in test mode. Floatinghotel now has an isolated
hidden-window dock option and commands that wait for actual dimensions. A
reusable native-window driver with an await-layout/presentation barrier would
help resizable inspectors, editors, game tools, and dockable panels. Pending
window changes must not overwrite remembered expanded geometry. The app keeps
that guard beside its dock state and leaves Afterhours unchanged.

### Isolated review persistence in native journeys (step 57)

Test mode deliberately disables review persistence. Restart tests for saved
baselines need the production persistence path, so an explicit test flag now
requires isolated settings before enabling it. An upstream E2E facility for
isolated persistent storage would let editors and games test saves and resume
without bypassing persistence or touching a user's normal data. The snapshot
view also now avoids binding the retained document's reading state; that was
app-owned state wiring, not an Afterhours renderer defect.

### Isolate every persistent provider path (step 58 correction)

`ProvidesResourcePaths::config_folder_path` and `save_folder_path` are separate.
The first review-persistence probe isolated only configuration; its fixture
records reached the ordinary save root. Test startup now redirects both fields,
and the native regression asserts every loaded review path is inside its
fixture. An upstream isolated-storage test scope should redirect configuration,
saves, and temporary artifacts together and restore them on scope exit. Eight
verified step-57 fixture records were removed; normal reviews were untouched.
### Composer focus and reading-anchor ownership (step 59)

An inline text editor changes its parent reader's content height. Restoring the
code anchor during that change scrolled the editor's Add button out of view at
200% zoom. The app now suspends automatic code-anchor restoration and sampling
while its comment composer is active, then explicitly returns to the code range
on save or dismissal. Short-window checks also require revealing the editor
after matching layout, including after a resize. Manual scrolling cancels that
frame’s automatic reveal. This is application focus/anchor policy. A reusable
scroll-anchor API should let a temporary child editor claim viewport ownership;
inspectors, annotation tools, and game dialogue editors need the same handoff.
The failing native layout and the repaired button geometry are retained with
step 59 evidence.

## Consolidated persistence and historical UI reports

The old persistence proposal, UI footguns list, and framework-issues document were
reconciled against the current checkout on September 13, 2026. Open implementation
work is tracked in [triage.md](../triage.md#afterhours-work). This section preserves
the remaining contracts and the limits of that source inspection.

### Atomic writes and durability — RESOLVED upstream, adopted by the bump
`afterhours::files::write_string_atomic` now creates a unique temp per write,
handles partial writes/`EINTR`, preserves target permissions, fsyncs the file
(`F_FULLFSYNC` on macOS), renames, and fsyncs the directory. Review JSON,
snapshots and Settings already call it, so the `5383fc6` bump adopted the
durable implementation without app changes. Settings' 250 ms coalescing and
shutdown flush remain app scheduling around that helper.

### Historical UI reports — retired against the pinned revision (October 6, 2026)

Both were re-run against the pinned `vendor/afterhours`; neither reproduces, so
neither is an upstream request.

- **Nested scroll.** Rows vanished when a panel nested in a scroll view resolved
  its content height to zero. `tests/unit/test_nested_scroll_layout.cpp` lays out
  a children-sized panel in a scroll view, the same inside a second scroll view,
  and two stacked sections in one scroll (the combined Changes page): each panel
  keeps all 600 px and its 30 rows keep size and order. The harness does fail
  when the panel is forced to zero height.
- **Invalid autolayout mapping.** The old startup crash came from a parent
  listing a child that was no longer in the mapping. `AutoLayout::autolayout`
  now calls `prune_stale_children` before any traversal, and upstream's
  `stale_child_id_is_dropped`, `stale_subtree_is_dropped_whole` and
  `stale_child_with_text_sizing_is_dropped` cover it; the pinned
  `tests/autolayout_test.cpp` passes 358/358 here. `to_ent` still logs and then
  dereferences, but its remaining callers only pass ids of widgets already
  reached through pruned children.

Several old claims are obsolete: `ComponentConfig` defaults to `FlexWrap::NoWrap`,
ECS iteration uses indices with a captured entity count, simulated clicks support
auto-release, pixel corner radii and measured text exist, and native offscreen
rendering works. They are not new TODOs. Current font-fallback, styled-text, scroll,
input, and headless limitations remain in their detailed entries above.


## CP-006: Configurable overscan and reusable row metrics — landed upstream
The retained measured-row index, reusable row metrics and configurable
overscan described by the prepared patch are in `5383fc6` in evolved form
(`HasVirtualListIndex`, measured `virtual_list`, and
`with_virtual_list_overscan`). FloatingHotel uses the upstream uniform and
measured lists directly; it has not needed a non-default overscan value. The
historical benchmark in `triage-evidence/afterhours-overscan.log` remains the
geometry evidence for why viewport coverage matters.

### Persistence composition encountered during triage

Afterhours components deliberately cannot be copy-assigned. A review component
also owns a cancellable task. Decoding settings/reviews transactionally therefore
uses a temporary data value and publishes the persistent fields only after the
whole document validates; it does not attempt to copy ECS ownership or worker
state. A reusable versioned storage adapter with explicit decode/error results
would benefit editors and games. Existing atomic file writes are reused; no
second file-writing primitive was added to the app.

The triage replay exposed a modal lifecycle constraint: the open-to-closed
transition must run through the same `mk` call site, even when the owning document
or repository changes. `mk` includes source location in identity; repeating a
numeric ID at another call site creates a different entity and leaves the original
modal's input gate active. The app uses one shared dialog-parent helper and explicit
closing transitions. Commit details, Push, and Relink also participate in the app's
semantic Escape/focus restoration. Automatic removal of unrendered modal owners
would make this safer for other applications. Context menus must render above modal
content; the app now gives them a higher layer.

The native modal backdrop pass also dims modal contents after the UI draw. These
reading dialogs use a transparent framework backdrop and an explicit ECS backdrop
below their panel, matching the existing Quick Open workaround. The framework's
backdrop dimensions also scale twice under UI zoom; the explicit backdrop uses
logical dimensions. The native renderer should own one correctly ordered backdrop.

### Tooltips for truncated labels

Renderer-owned truncation now exists as `HasLabel::truncated`, and the app's
filter reads it instead of re-measuring the label. The remaining app piece is
the owner-to-label association: several rows put the tooltip on the row while a
child label is the thing that ellipsizes, whereas upstream suppression only
compares tooltip and label on the same entity.
Tooltip anchors also need lookup through `UICollectionHolder`: looking up a UI
entity ID in the default ECS collection can leave valid tooltip state without a
visible tooltip. The app's wrapped-tooltip renderer now uses the UI collection.

### Pinning an interactive header

The UI plugin has no sticky-child layout primitive. A translation modifier moves
only the entity that owns it, so applying it to a file header leaves child labels,
buttons, clips, and hit targets behind. The reader keeps the original header in
normal flow and, after scroll measurement, adjusts the computed positions of its
whole subtree. Its next sibling bounds the pinned position. The same adjustment
runs after scroll input so drawing uses the current scroll offset; layout resets
it on the next frame. Header controls retain their entity IDs and render above
code, below temporary overlays. A framework sticky-child policy that preserves
flow geometry, clipping, descendants, and input would also help sectioned lists,
inspectors, and game inventories.

Sticky controls also need to publish their occupied reading area. At 200% zoom,
a full header can cover a Find result placed at the old fixed viewport fraction.
The app exposes a reading viewport inset and uses it consistently for anchors,
Find/caret destinations, source origins, and selection hit tests, while retaining
the full scroll viewport for layout and scroll extents.

### Weighted runs in styled labels draw small

A `TextSpan` with `FontWeight::Bold` (resolved to a registered `"<font>@bold"`
face, here Roboto-Bold) draws at roughly three quarters of the surrounding
regular runs' size in the batched styled-label path, although both receive the
same `font_size` (`result.rect.height` from the regular face). Whole labels
using the bold face through `with_font("ui-bold", ...)` look right. The Markdown
preview therefore strips `**bold**` markers but draws the text at regular
weight; switching its span to `FontWeight::Bold` is a one-line change once
weighted runs match their neighbours.

### Text areas and custom backgrounds — RESOLVED upstream
`text_area` now honors a caller's custom/transparent background before falling
back to `Theme::Usage::Secondary`, matching `text_input`.

### E2E `CMD+` — RESOLVED upstream
`parse_key_combo` now maps `CMD+`, `SUPER+`, `WIN+` and `META+` to the distinct
Super modifier instead of Control. FloatingHotel also migrated its held-key
scripts from numeric codes to upstream key names and deleted its duplicate
`hold_key` handler.

### Absolute children of a scroll view count as content

`update_scroll_view_content_size` sums every child of a scroll view, and
`apply_scroll_anchor` can pin any of them, absolute ones included, although
autolayout skips absolute children everywhere else. A border overlay hung
directly off the commit view's scroll container grew its scroll range by the
overlay's height and became the scroll anchor, so opening diff Options scrolled
the summary out of view (`flow_commit_detail_regression`). Also, a uniform
1px border's outline lands outside its rect at 1x, so the scroll scissor clips
an edge flush with the viewport. The diff's file-card overlays therefore hang
off the zero-height `diff_headers_end` marker and are inset 1px. Skipping
`absolute` children in both loops would let overlays sit in the scroll view.

## Zed-parity research (November 15, 2026)

Found while working through [todo.md](../todo.md). None of these block the work;
each is a workaround in app code that upstream could make reusable.

### Shortcut chords have no display label or introspection

`input::KeyChord` already carries `MOD_SHIFT`/`MOD_CTRL`/`MOD_ALT`/`MOD_SUPER`,
but floatinghotel still hard-codes ~30 shortcuts as `is_key_down` +
`is_key_pressed` pairs across `main_content_system.h`, `tab_bar_system.h`,
`menu_bar_system.h` and `document_switcher.h`. The shortcut sheet
(`keyboard_shortcuts.h`), menu labels (`menu_setup.h`) and the command palette
each repeat the same chords as hand-typed strings, and they drift: the sheet was
missing nine bindings. Moving to `GameMapping` would fix dispatch, but the app
would still need (a) a formatter from a `KeyChord` to a platform label
("⌘⇧P" / "Ctrl+Shift+P"), (b) a way to enumerate an action's chords so the
sheet, menus and palette render from the mapping, and (c) a conflict check for
two actions bound to the same chord. Workaround: strings kept by hand.

### Double-click on a clickable div

**2026-11-26 status:** `UIContext::is_double_click` /
`is_triple_click` and `mouse.click_count` landed. FloatingHotel keeps its two
semantic sequences for now: navigation must treat the same document/location
as repeated across rebuilt entities, and diff selection acts on press rather
than upstream's release-time count. A press-time, target-independent click
streak would be needed to delete both.

`HasClickListener` reports `down` but not a click count, so each double-click
site keeps its own last-target + timestamp (500 ms): document tabs in
`navigation.h` and now branch rows in `sidebar_system.h`. `text_area` already
tracks `click_count` internally. Exposing a click count (or `double_clicked`)
on `HasClickListener`, using the platform double-click interval, would replace
both and match the OS setting.

### Background work handoff for immediate-mode pickers

**2026-11-26 status:** FloatingHotel's picker already moved matching to a
worker with generation checks and confirm-when-ready. Upstream `jobs::Pool`
does not yet provide the latest-wins pollable slot described here.

Zed's pickers match off the UI thread, keep the previous results on screen until
the new set lands, cancel stale searches with a flag + generation id, and queue
an Enter pressed mid-search. floatinghotel's Quick Open ranks on the main thread
per keystroke. `async_task.h` in the app covers one-shot jobs. A small upstream
"latest-wins" async slot (submit with generation, cancel previous, poll result
on the frame) would serve Quick Open, the palette, repository search and the
repo picker alike, plus any game that filters large lists.

### `children()` height ignores wrapped rows

A row container with `with_wrap()` and `ComponentSize{percent(1.f), children()}`
lays its overflowing children onto a second line, but its own height stays one
line tall, so the wrapped line draws over the next sibling. Seen with the Quick
Open category chips in the narrow picker opened from the repository `…` menu
(November 16, 2026). Workaround: keep the fixed-height row and avoid wrapping.
Sizing a wrapping container's cross axis to the sum of its line heights would
let chip rows and tag lists wrap safely.

## Bump b385dc9 → e9d5f67 (November 16, 2026)

228 upstream commits. No afterhours code was changed; the app adapted:

- `ui::detail::apply_scroll_offset` → `apply_ancestor_transform` (545de11; now
  also composes parent scale/translate). Renamed at every call site.
- `ui::RenderScrollbars` is gone (6931c61): the renderer draws each layer's
  scrollbars itself. `RenderFloatingScrollbars` was deleted and the floating-UI
  renderer now sits at bridge index 2 (`[held, RenderImm, floating, debug]`).
- `perf_commands::builtin_profile::totals()` → `profiling::default_collector()
  .snapshot().systems` (`bench_frames`, `dump_profile` provider).
- `measure_text_internal` dropped (30c6ad6): the `font_atlas_ok` probe measures
  through `FontManager` + `measure_text` directly, bypassing `TextMeasureCache`.
- No backend define now means raylib (729cbb0). The unit runner passes
  `-DAFTER_HOURS_USE_NONE`; previously the absence of a define meant "none".
- The E2E parser hands quoted arguments over already unquoted. The app's
  `touch_file … text`, `show_toast`, `native_menu_action` and
  `expect_review_export` re-joined args and parsed them with `std::quoted`, so
  only the first word survived. `e2e_text_arg` accepts both forms.
- Labels clip to their own box (6327e72). `tests/commit_template.py` asserted a
  template body line that only showed because text overflowed the one-line
  commit box; it now relies on the committed message for the body.
- Layout overflow/wrap warnings are logged once per (child, parent) (f9d5b77),
  which ends the Quick Open log flood noted above.

Found during the bump, app-side: `CollectCurrentResolution` was never
registered, so `ProvidesCurrentResolution` kept the startup window size (280 by
default) and every modal, including the framework backdrop, was sized for it.
Now registered in `main.cpp`. The "`children()` height ignores wrapped rows"
entry above was observed on b385dc9 and has not been re-checked on e9d5f67.

## Unstaged mock comparison (November 19, 2026)

Built `docs/mocks/unstaged_mock.html` as the target for the aggregate
Unstaged changes document, captured the same scene from the app
(`/tmp/mock_compare/aggregate.png`, `aggregate.json`), and compared them
(an independent review pass concurred on all three entries below).

### Label text origin cannot be derived from the component config

**2026-11-26 status:** upstream now exposes `resolved_text_origin`, including
the resolved inset and laid-out rect. FloatingHotel's diff rows explicitly set
the same 5px inset that `diff_sel::content_x_offset` names, so no app change was
required for correctness; adopting the query at overlay sites is optional
cleanup if those rows ever stop sharing one inset.

Extends "Text inset is hardcoded…" and "Immediate plain labels ignore the
configured text inset" above with fresh measurements on e9d5f67. A label's
`Padding{.left}` never offsets its own text in either draw path. The styled
(span) path draws at the configured `text_inset` (default 0); the plain
path draws at a fixed ~5px. Nothing exposes the resolved text origin, so
an app that overlays its own geometry on label text (caret, selection,
find match, intraline-change tint in `diff_renderer.h`) must guess per
label style. Measured on a 6px-advance mono row: glyphs at x=396, the
intraline overlay drawn at x=401 — a 5px shift that put the changed-file
highlight on `]` instead of the changed character — while the app-side
constant assumed the plain path's 5px. The hunk header's `@@` also sat on
the card border (padding-left 12 ignored, drawn at inset 0 inside an
`Overflow::Hidden` clip) and lost its first glyph. Workarounds now in the
app: explicit `.with_text_inset(5.f)` on diff lines and
`.with_text_inset(12.f, 0.f)` on hunk headers, keeping the 5px assumption
in `diff_sel::content_x_offset`. Drawing every label path at its
configured inset and exposing the resolved origin (or a text-rect in the
layout snapshot) would remove the per-style guessing.

### No bordered, clipped container for a range of virtualized rows

A diff file card is dozens of sibling rows (header, hunk headers, lines,
footer), individually culled, so there is no container entity that can
carry the card's border or clip the rows to its rounded corners (the
earlier "Absolute children of a scroll view count as content" entry covers
the workaround's cost side). The app paints the border as an absolute
overlay (`file_card_border`, render layer 49) on top of the rows: any
glyph that reaches the card edge — see the entry above — is overdrawn by
the border stroke, and row backgrounds are not clipped to the corner
radius. A first-class "bordered region over a row range" (or a container
whose border does not participate in content size/anchoring) would let
virtualized lists have real cards instead of overlay strokes.

### Layout overflow warnings report zero-size parents

During normal first frames, autolayout logs warnings such as
`Layout overflow: 'fold_file:a.cpp' extends outside parent '' bounds
(child_size=[28.0,28.0], parent_size=[0.0,32.0])` and several
`file_header_btns` children overflowing a `[0.0,64.0]` parent. The parents
are `children()`-sized containers whose measured size is still 0 on the
measuring pass, so the warning fires on geometry that settles correctly a
pass later. It is logged once per (child, parent) pair, but on a fresh
scene that is still dozens of false positives, and it sent a visual
investigation (the mock comparison above) looking for a real overflow
that did not exist. Suppressing (or downgrading) overflow warnings while
either axis of the parent is still unresolved/zero would keep the signal
for genuine overflows only.

## Bump e9d5f67 → 5383fc6 (November 26, 2026)

1,616 upstream commits, direct fast-forward. The application adaptations in
this bump were intentionally deletions first:

- `Modal::previously_focused_element` is an `EntityHandle`; use
  `EntityHandle::invalid()` instead of `-1`.
- Deleted FloatingHotel's `metal_set_window_size`,
  `metal_window_resize_pending`, live-resize draw swizzle and resize-draw
  policy. The Sokol backend now owns all three. The only remaining startup
  timer is app-owned because FloatingHotel deliberately keeps its window
  unordered until the first repository refresh is ready.
- Deleted `src/ui/text_area.h`, `src/ui/virtual_list.h`, the pending-draw
  clear, scroll-input/scrollbar-drag subclasses, texture-handle stripping,
  text-field focus child walk, directional-input marking, and the numeric
  E2E held-key handler. Their upstream replacements are named in the summary
  at the top of this file.
- Centered dialogs no longer assign `absolute_pos_x/y` after `modal_impl`;
  the framework centers in screen space at any `ui_scale`. Picker dialogs that
  intentionally sit at 30% height keep their explicit placement.
- The commit box uses the now-working `text_area` placeholder instead of a
  separate hint label.
- Three existing `with_padding(Padding{})` calls were audited against the new
  explicit-padding semantics and left as explicit zero padding where that was
  their intent.
- E2E scripts adopt the new upstream contracts where they changed: scoped
  `assert_ui_text` instead of ambiguous repeated debug names, and
  `expect_unread_key` for presses a flow intentionally proves are ignored.
  The app's first-match click handler now waits on upstream's render
  checkpoint. The full-suite comparison also exposed an app bug in the file
  tree viewport anchor: a section header at the top produced a clamped
  fraction that scrolled the header itself away; the fraction is now allowed
  to be negative relative to the first real row.

Behavior to keep watching after this bump: Tab now follows UI-tree order,
Escape has one framework owner, E2E dispatch is serial and ambiguous lookups
fail loudly, and `Dim::Text` measures the drawn face/spacing. Those are
upstream behavior changes, not app workarounds to reintroduce without a failing
replay.

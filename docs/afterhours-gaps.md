# Afterhours Gaps & Missing Primitives

Tracking afterhours features/bugs that floatinghotel needs but are not yet implemented upstream. Each entry includes what's missing, what floatinghotel needs it for, and the workaround.

**Policy:** Never edit `vendor/afterhours/` directly. Build workarounds in `src/ui/` and document gaps here so the afterhours maintainer can address them upstream.

**Reviewed against the cf3e0f1 bump.** Several entries here were stale — the
API had shipped and nobody came back to the doc. Anything below still marked
OPEN was checked against that revision.

Adopted in that pass: `with_corner_radius`, `imm::divider`,
`ctx.is_right_click` + a real context menu, `afterhours::shutdown()`,
`ui::measure_text_line`, trackpad pinch and `Theme::ui_scale` zoom.

Deliberately not adopted, with reasons in the entries below:
`with_styled_label` (child divs already do it better here),
`hsplit_pane`/`vsplit_pane` (panels are absolutely positioned, no container to
wrap), `animation::set_instant` (the animation plugin is not registered — the
toast plugin does not use it), `with_font_weight` (needs bold font files).

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
The framework's row renderer owns one label and recursively builds every
expanded node. `TreeViewConfig` has no custom row callback or virtual row model.

A useful extension for file browsers, scene hierarchies, and game inventories
would accept a row callback plus stable node keys, expose the flattened visible
rows, and reuse `virtual_list`. Selection, keyboard focus, and expansion need
separate state. Left/Right tree traversal and type-to-select belong in that
shared controller. App-specific review indicators stay in the row callback.

Status on 2026-09-12: API availability and the missing extension points were
verified from source. This audit did not render the upstream widget or prove
keyboard or performance behavior. Existing app tree captures are described in
`A hover-styled row needs to own its click target` below.

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

<details><summary>original entry</summary>

### No Rich Text / Multi-Color Text in a Single Label — OPEN

**Problem:** Each `div` or `button` can only have one text color. To show a filename in white and its directory path in gray on the same row, you need two separate child `div` elements.

**Workaround:** Bake status letter and filename into a single label string (e.g. `"M  README.md"`). Colored status letters are sacrificed.

**Suggested fix:** Support a `StyledText` API:
```cpp
.with_styled_label({
  {"M ",        theme::STATUS_MODIFIED},
  {"theme.h ",  theme::TEXT_PRIMARY},
  {"src/ui",    theme::TEXT_SECONDARY}
})
```
</details>

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

### text_input() requires InputAction enum values
- **Issue:** `text_input::text_input()` template expects `InputAction::TextBackspace`, `TextDelete`, `TextHome`, `TextEnd` enum values, which are not part of afterhours and must be defined by the host app.
- **Impact:** Cannot use `text_input()` without adding these to the app's `InputAction` enum and registering key mappings.
- **Workaround:** Added the required enum values to `src/input_mapping.h` and registered key mappings in `src/preload.cpp`.

### Clipboard shortcuts not wired in text_input — RESOLVED, adopted
- afterhours implements copy/cut/paste/undo/redo/word-motion/shift-selection and
  binds them in `ui::default_keymap<InputAction>()`, matched to the app's enum
  **by name**. A name the enum does not have compiles the feature out silently,
  which is how the commit box went without them for so long; the library warns
  about the missing names now.
- `src/input_mapping.h` carries all eleven, and `src/preload.cpp` takes
  `default_keymap` instead of hand-listing four keys.

---

### Draw capture is not implemented on the sokol/Metal backend — OPEN
- **Issue:** `capture::record()` is called from `backends/raylib` and
  `backends/none` only. `backends/sokol` never records, even though the UI
  renderer sets up `capture::Scope` attribution around every draw.
- **Impact:** `expect_drawn`, `expect_not_drawn`, `expect_drawn_at` and
  `dump_draws` are no-ops here — `dump_draws` reports `0 draws this frame`.
  `tests/e2e_scripts/flow_commit_graph_lines.e2e` is named for asserting that
  graph lines are drawn and can only take screenshots.
- **Suggested fix:** call `capture::record` from
  `backends/sokol/drawing_helpers.h` the way the other two backends do.

### `with_placeholder` is a text_field feature; text_area ignores it — OPEN
- **Issue:** `text_area()` never reads `config.placeholder`. Setting it
  compiles, runs, and shows nothing.
- **Impact:** A multi-line field cannot have a hint inside it. The commit box
  keeps its hint as a label stacked above the input, costing 16px of sidebar.
- **Suggested fix:** either honour `placeholder` in `text_area`, or reject it
  at the call so it fails loudly instead of silently.

### text_area lines overflow their own field — OPEN
- **Issue:** `text_area_line` is laid out wider than the `text_area_field` that
  owns it, e.g. `child_size=[344.4,0.0]` inside `parent_size=[312.0,52.0]`. The
  line is also reported with height 0.
- **Impact:** The only layout warning floatinghotel still emits comes from
  inside the library's own widget, so the containment check is noisy for every
  consumer with a text area. Nothing visibly wrong on screen — the field clips
  — but it makes `assert_within_parents` harder to trust.
- **Repro:** the commit message box and the review comment box, any run of the
  e2e suite.

### Font codepoint coverage helpers are raylib-only — OPEN
- **Issue:** `default_codepoints()` and `font_has_glyph()` live in
  `backends/raylib/font_helper.h`. The sokol backend rasterises on demand
  through fontstash and has neither.
- **Impact:** No way to ask whether a face covers a codepoint before using it,
  so the ASCII fallbacks in `sidebar_system.h` (`"> "` for ▶ at the review
  strip, no arrows on Push/Pull) stay guesswork.

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

### capture_impl.h leaks a staging texture per screenshot unless built with ARC — OPEN, worked around
`metal_resolve_msaa` and `metal_copy_to_shared` create their readback texture
with `newTextureWithDescriptor:` (+1) and return it; `metal_read_image_pixels`
never releases `staged`. Under ARC that is fine; in a plain `-ObjC++` build it
leaks one 1280x720 RGBA texture (3.7 MB) per `capture_frame`. Measured
2026-09-10 with `footprint -p`: the E2E batch's IOAccelerator memory grew in
step with the PNG count (465 MB after 124 screenshots, still one script in)
and the suite passed 1 GB. wm is raylib, so upstream never runs this path.
- **Suggested fix:** either state the ARC requirement next to the
  `SOKOL_IMPL` instructions, or make it build-mode independent:
  ```objc
  #if !__has_feature(objc_arc)
    [staged release];
  #endif
  ```
  after `getBytes:` in `metal_read_image_pixels`.
- **Related:** headless mode has no sokol_app, so nothing wraps a frame in an
  autorelease pool; the command buffers and encoders sokol_gfx autoreleases
  each frame accumulate until exit. Worth a note in the headless docs, or a
  `graphics::headless_frame(fn)` that provides the pool.
- **Worked around app-side:** `src/sokol_impl.mm` now builds with
  `-fobjc-arc`, and the headless loop runs each `app_frame` inside
  `metal_headless_frame()`, an `@autoreleasepool` wrapper. Full suite after:
  IOAccelerator flat at 21-30 MB, peak footprint 167 MB, 98/98 in 3.5 min.

### Windowed startup is spent waiting on sokol_app, not in app code — OPEN
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

- **Suggested fix (sokol_app, macOS):** at the end of
  `applicationDidFinishLaunching`, after `makeKeyAndOrderFront`, call
  `_sapp_macos_frame()` (or `[_sapp.macos.view draw]`) once so init and the
  first frame run synchronously instead of waiting for the display link.
  Saves 65-160 ms per launch. **Worked around app-side:**
  `metal_draw_first_frame_early()` in `src/sokol_impl.mm` registers a
  DidFinishLaunching observer (from inside WillFinishLaunching, so it lands
  after sokol's delegate) and calls `[view draw]`; the log line
  `First frame drawn from applicationDidFinishLaunching` confirms it fired.
- **Font loading reads the file per call:** `load_font_from_file` is
  `fonsAddFont(ctx, file, file)` with no dedupe, and there is no
  `fonsAddFontMem` path, so a font cannot be embedded in the binary. The app
  now aliases `SYMBOL_FONT` to the already-loaded `DEFAULT_FONT` instead of
  reading Roboto twice. A `load_font_from_memory` would let the three TTFs
  ship inside the executable and skip three scanned file opens at startup
  (86-184 ms cold on this machine).
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

### E2E target lookup needs a render checkpoint after cached view transitions

- Status: host integration constraint, reproduced during review item 01 on b385dc9. Not a claim that ordinary pointer input is broken.
- Reproduction: open a commit file, return to the diff, open working-tree content, return to the same cached commit, then use `click_ui open_full_file`. In floatinghotel's batched headless loop, the final click can leave the commit diff open. Five logic-only frames do not fix it. Capturing a screenshot before that click does.
- Boundary: `src/plugins/e2e_testing/ui_commands.h` filters target lookup with `UIComponent::was_rendered_to_screen`. The host's `e2e_tick_loop` runs multiple `tick_all` calls before a render. A completed model refresh therefore does not establish that the target view has been drawn.
- App workaround: `tests/review_50/item_01.e2e` takes `item_01_commit_again` before the repeated open, then asserts `full_file_header`. Text assertions alone can match code in the wrong view.
- Maintainer request: expose a render-generation checkpoint for UI commands, or document the required render boundary when a host batches logic ticks. A target command should wait for the current view generation rather than use an earlier drawn view.
- Additional reproduction in item 41: resizing from 1280 to 960 before `assert_no_overflow` reports an obsolete `full_file_loading` rectangle at x352 with width928, even though loading has finished. `output/review-50/item-41.log` fails; adding a screenshot checkpoint before the assertion passes in `item-41-drawn.log`. The inspected screenshot shows no loading row or overflow. Validation also needs a current render generation.
- Startup reproduction: `expect_text "Timed commit"` followed by `click_text "Timed commit"` dispatches the click before the text assertion settles. `runner.h` retries pending assertions without blocking subsequent commands, while `click_text` fails immediately if its target is absent. No blocking text-or-UI wait command was found in the pinned E2E plugin. Use the host's `wait_for_refresh` and a render checkpoint for ordinary async navigation. The windowed startup regression instead clicks immediately after the host presentation gate has drawn the ready UI. This is test sequencing, not an ordinary input defect.

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

### Retained texture components survive a texture-free immediate widget — OPEN

`ui/component_init.h::apply_texture` returns when a new `ComponentConfig` has
no texture, leaving an earlier `HasTexture` on the reused UI entity.
The renderer still submits that handle after its owner calls `unload_texture`.
On Metal this aborts with `VALIDATE_ABND_VIEW_ALIVE` and
`VALIDATE_ABND_SAMPLER_ALIVE` rather than completing an image-to-text transition.

Reproduction: `bash tests/review_50/item_12.sh` opens the changed BMP, waits for
both previews, switches to `source.txt`, then reopens the BMP. Before the app
workaround, the first switch aborted after `item_12_async_images.png`.

The app now removes `HasTexture` components referencing its exact retiring
image handles before unloading those textures. No unrelated texture is removed.
Upstream should clear absent texture configuration as it already clears absent
shadow configuration, and provide a safe way to retire textures referenced by
retained immediate UI entities.

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

### Virtual-list metrics mix physical and logical pixels in Adaptive mode

`ui/imm_components.h::virtual_list_impl` uses physical scroll offsets and viewport
height to index supplied row metrics. It then places the same row and leading
spacer metrics inside `pixels()`, which applies `ui_scale` again in Adaptive mode.
The trailing `unbuilt_content_size` remains physical. At non-default zoom, callers
cannot satisfy both contracts by changing the supplied row height alone.

`src/ui/virtual_list.h` accepts logical row heights and passes physical metrics to
the existing list implementation. It normalizes only the generated wrapper and
leading-spacer desired heights before layout. The list's window selection,
recycling, and physical trailing extent remain upstream-owned. The adapter uses
`UICollectionHolder` to resolve generated UI children and does not change global
zoom or vendor files.

The app also sets `skip_grid_snap` on the list and generated children. This flag
does not fully disable flow-position snapping: `autolayout.h` still snaps each
child's position and the running row offset. A 42-physical-pixel row can therefore
have a 44-pixel pitch on a four-pixel grid. Upstream needs consistent units for
virtual-list metrics and a flow-position policy that honors `skip_grid_snap`.

The native review-focus zoom test exercises 100%, 140%, and 160%, including list
scrolling and footer geometry. The first adapter revision used `EntityHelper`
instead of `UICollectionHolder`; that lookup choice is an app issue, not an
additional upstream gap. This app currently enables the single-collection mode.

### Font-size tiers do not follow Adaptive zoom

`ComponentConfig::with_font_size(FontSize)` converts each tier through `h720`,
which is screen-relative and does not apply `ui_scale`. In the first native
review-focus capture at 140%, the sidebar's tier-sized text stays small while
pixel-sized controls grow. See `output/review-focus/first-integration/zoomed_bottom.png` from
the first integration run.

The review UI now uses explicit logical pixel font sizes. Code fonts, row
heights, selection measurements, and virtualization use the same zoom factor.
Upstream could resolve semantic font tiers through the selected scaling mode.

### Keyboard-only focus rings are not exposed

The current `HighlightMode` options are `Split` and `FollowsMostRecentInput`.
`ComputeVisualFocusId` retains a visible focused widget after mouse selection,
so clicking a commit leaves a heavy outline unlike the approved mock.

The app keeps keyboard focus accessible and uses a thinner accent-colored ring.
It does not suppress focus rendering. A keyboard-only focus-visible policy
would let pointer selection keep its normal selected-row styling.

### Per-component grid opt-out does not preserve exact flow spacing

The review-focus layout disables grid snapping through the existing application
styling setting. This keeps logical row heights and flow offsets consistent
through zoom. The `skip_grid_snap` limitation described above remains upstream;
no copied layout engine or vendor patch is needed for this workaround.

### E2E hidden assertions cannot check a removed widget

Closing the source tab removes its immediate-mode widget. The test command
`assert_ui content_source_tab hidden=true` then times out looking for that widget
rather than succeeding on its absence. The failed run is preserved in
`output/review-focus/first-integration/tabs.log`.

The app test property `ui_present:<debug name>` checks whether an actual widget
was rendered, so the close-tab regression can assert `false`. An upstream
absence assertion would avoid each app adding this check.

### Text-area line widths are scaled twice at increased zoom

The 140% hex-view regression also exposes an independent text-area warning in
the Files sidebar. `output/review-focus/regressions/item-43.log` records a
`text_area_line` width of 500.6 inside a 357.6-wide field, a ratio of 1.4.
In `src/plugins/ui/text_input/text_area.h`, `viewport_width` comes from the
field's computed physical width, then becomes `pixels(viewport_width)` for
each generated line. Adaptive layout applies zoom to that value again.

The field clips overflow, but line sizing and wrapping should share one unit
system. The app now routes all three text-area callers through
`src/ui/text_area.h`. After creating the framework field, this adapter gives
its generated `text_area_line` children a width of `percent(1.f)`.
The rows follow the available content width without converting a computed
physical width back into logical pixels. Cursor and selection children are
not changed. No framework implementation is copied or edited.

`tests/check_text_area_layout.sh` fails before this adapter and passes after it
at 100% and 140%, including zoom reset and preserved draft text. The basket
compose/edit flow and code-highlight tests also pass without text-row width
warnings. This workaround does not claim to fix other text-editor geometry.

### Text inset is hardcoded and separate from component padding

`draw_text_in_rect` in `src/plugins/ui/rendering.h` accepts `text_inset`,
but positions single-line text with a hardcoded five-physical-pixel margin.
`draw_runs_in_rect` duplicates that margin to align styled labels.
Component padding affects absolute child positions, not the label's origin.

Our selection code added eight pixels for label padding, then the layout engine
added that padding again to the highlight child. A native source-view test
placed the highlight at x=370 while the first character's advance starts at
x=359. The same drag copied nine characters instead of ten. This padding
assumption was an app bug.

The app now gives code rows zero padding and shares a single content-origin
calculation across hit-testing, selection, search, and intraline highlights.
`diff_sel::content_x_offset` accounts for the renderer's fixed five-pixel inset
without scaling it again. No vendor files are changed. Upstream should use the
configured inset consistently and expose the resolved text origin for overlays
and hit-testing, including both immediate and batched rendering paths.

Reproduce with `nice -n 10 bash tests/check_text_highlight.sh`.
The failing baseline is in `output/text-highlight/before-exact/native.log`.

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

### Text-area line height reads the raw Size value

`text_area` reads `config.text_area_line_height.value` without resolving its
unit. Passing `h720(18)` supplies `18 / 720`, so the generated rows measure
0.025 logical pixels instead of 18. The native inspector rounds that to zero.
The commit-message text consequently sat against the field's top edge.

The app now passes `pixels(18)` for this setting. The framework API accepts a
`Size`, so it should either resolve that size or accept an explicitly named
pixel value. This caller workaround does not change the framework.
The failing native checks are in
`output/layout-followup/line-height-before/native.log`.
The passing check in `output/layout-followup/line-height-after/native.log`
measures 18 pixels at 100% zoom and 25.2 pixels at 140%.

### Divider hit area and visible thickness share one rectangle

`imm::divider` uses its component rectangle for both drawing and hit-testing.
It has no separate hit-area inset. The app's one-pixel separator above commit
history therefore had a one-pixel drag target. This was an app sizing mistake,
not a failure of framework pointer capture.

The app now composes a 16-logical-pixel divider with a centered one-pixel child
line. This keeps the line thin without taking clicks from adjacent rows.
Both sidebar modes use the framework's movement delta, divided by UI zoom,
instead of snapping the split to the pointer's absolute position. The absolute
position calculation was also an app bug. No vendor files are changed.

An independent hit-area inset would make thin splitters easier to implement.
The native regression script is `tests/review_focus/commit_splitter.e2e`.

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

### Skipped redraws retain earlier UI draw commands

At Afterhours revision `b385dc9`, `BeginUIContextManager` does not clear
`UIContext::render_cmds`. The immediate and batched renderers clear it only
after drawing. Updating the UI without rendering therefore retains commands
from every skipped frame.

The app's idle frame pacing exposed this assumption. A native capture recorded
252 UI commands on an ordinary redraw and 3,276 after 12 skipped redraws.
The overloaded frames lost menu text and much of the sidebar. Captures are in
`output/text-flicker/before/idle_001.png` and `idle_003.png`.
The renderer emitted no overflow warning during this reproduction.

The app now registers `ClearPendingUIDraws` before the framework's UI update
systems. It clears the prior frame's queue before any widgets are rebuilt,
including normal updates, test ticks, and benchmark frames. Idle redraw skipping
remains enabled. No vendor files are changed. Upstream should scope the draw
queue to a UI update, not require a render after every update.

The previous screenshot tests forced extra redraws before capture and missed
the bad frames. `capture_idle_frames` now captures each redraw inside the idle
benchmark without forcing additional draws. Run
`nice -n 10 bash tests/check_text_flicker.sh` to compare text pixels and command
counts across those frames. The checker requires Python with Pillow.
Each run keeps its captures in a separate directory.

The fixed run in `output/text-flicker/after/run.ENEZrH` captured 15 redraws
across 120 updates, with 105 redraws skipped. Every captured frame has 252 UI
commands and identical menu, sidebar-header, and code pixels. The failing
pixel check is reproducible against `output/text-flicker/before`.

Related verification passed: 74 UI flows in `output/text-flicker/flows.log`,
idle input and file-watcher checks in `output/text-flicker/idle-inputs.log`,
six frame-pacing unit tests in `output/text-flicker/frame-pacer-unit.log`, and
27 splitter assertions in `output/text-flicker/splitter.log`.

### Startup reports a missing toast singleton

Both the failing and fixed flicker runs log a missing
`afterhours::toast::ToastRoot` singleton during startup. The diagnostic does not
identify the caller. The app registers toast singleton enforcement before its
UI systems, so identifying the earlier lookup needs a separate trace. This
warning remains unresolved and is not evidence of the flicker cause. See
`output/text-flicker/after/run.ENEZrH/native.log`.

### UI dumps omit computed spacing

The framework's `dump_ui` writes an XML tree with rounded rectangles and
truncated labels. It does not expose computed padding, margins, or gaps, so a
spacing audit cannot distinguish nested insets from layout errors. The app adds
`dump_ui_json` and a JSON sidecar for each test screenshot. Both use the
framework's screen-rectangle calculation, including scroll offsets. The app
records unrounded geometry, spacing, parent links, and full text without vendor
changes. See `docs/ui-layout-dumps.md` for the schema and capture command.

### Immediate plain labels ignore the configured text inset

`draw_text_in_rect` receives the resolved `HasLabel::text_inset`, but its
single-line path constructs a new `{5, 5}` margin instead. At revision
`b385dc9`, `.with_text_inset(12, 0)` still draws five pixels from the left,
and `.with_text_inset(0)` does not remove that margin. The styled and batched
paths use the supplied inset. Layout padding also affects children, not the
component's own label. The spacing audit reproduced this in sidebar headers
and `commit_files_empty`; compare `output/spacing-audit/graph/graph_empty_commit.png`
with its JSON sidecar. The sidebar geometry fix does not change this renderer
behavior. A further workaround must preserve code-selection glyph alignment.

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
The focused captured layouts pass their geometry checks. The remaining warnings
have not been traced to a root cause and should not be dismissed as harmless or
reported as fixed. Some legacy Git controls also retain window-relative font
sizes, which makes their text small at enlarged zoom.

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
and navigation policy remain app-owned. The persistence proposal already lives
in `docs/afterhours-persistence-proposal.md`; it is not duplicated here.

The first upstream fixes to prioritize are already documented above: nested-scroll
coordinates and wheel input, styled-label ellipsis, mixed-height virtualization,
texture retirement, and native window resizing. They have concrete app evidence
and benefit menus, inventories, scene editors, log viewers, and asset browsers.
The entries below cover additional reusable code rather than reopen those bugs.

### U1. Bounded background jobs with cancellation and foreground capacity

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

### U6. Focus return that survives rebuilt controls and closed scopes

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

### U8. Read-only selection over a virtualized text source

`src/ui/diff_renderer.h::diff_sel` stores endpoints as entity IDs and byte
columns, rebuilds line records each frame, measures prefixes for hit testing,
and copies from `lastLines`. `ordered_span` cannot resolve an endpoint whose
rendered row has disappeared. Log viewers, chat transcripts, and scripting
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

### U9. Optional text normalization at file and clipboard boundaries

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
now retains a repository-owned destination DocumentId until its tab is built,
then explicitly focuses that tab. A reusable semantic focus request, resolved
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

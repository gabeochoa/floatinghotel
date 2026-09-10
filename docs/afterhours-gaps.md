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

### 3. Tree Node
- **What's missing:** No collapsible tree node widget for hierarchical list views.
- **What floatinghotel needs:** File tree view in sidebar (grouped by directory), branch list with expandable remote sections.
- **Criticality:** BLOCKER for P1 (tree view of changed files)
- **Workaround:** Built app-local in `src/ui/tree_view.h` using `div()` + `button()` with indent levels and expand/collapse state tracked in a static map.
- **Upstream request:** Add `tree_node()` to afterhours UI plugin with arbitrary nesting, expand/collapse animation, and arrow icon rotation.

### 4. Dropdown Menu
- **What's missing:** No dropdown menu widget (click to open a list of items below a trigger element).
- **What floatinghotel needs:** Menu bar dropdowns (File, Edit, View, Git, Help), commit button dropdown (amend/fixup), template picker, branch selector.
- **Criticality:** HIGH for P0 (needed for menu bar)
- **Workaround:** Built app-local in `src/ui/menu_setup.h` using `div()` + `button()` with absolute positioning. Manages open/close state, hover-to-switch between adjacent menus, and click-outside-to-close.
- **Upstream request:** Add `dropdown_menu()` to afterhours UI plugin with configurable items (label, shortcut text, separator, disabled state, callback).

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

### 6. Anchored Popup / Popover
- **What's missing:** No anchored popup that appears relative to a trigger element (above, below, left, right).
- **What floatinghotel needs:** Commit button dropdown (amend/fixup options), branch selector popover, tooltips for toolbar buttons.
- **Criticality:** MEDIUM for P0
- **Workaround:** Reuse dropdown menu approach from `src/ui/menu_setup.h` with manual position calculation relative to the trigger element's bounds.
- **Upstream request:** Add `popover()` to afterhours UI plugin with anchor element reference, placement preference, and auto-flip when near window edges.

---

## Styling & Layout Gaps

### Text Overflow / Ellipsis — RESOLVED (120a9ed)

`with_text_overflow(TextOverflow::Ellipsis)` on `ComponentConfig`. The renderer
binary-searches for the longest fitting prefix and appends "...".

The hang this entry reported with `expand()`/`children()` sizing is fixed
upstream, so the "fixed pixel widths only" restriction no longer applies.

---

### Div backgrounds render opaque — no alpha blend for overlays — RESOLVED, adopted

A `div` background alpha-blends now, so `with_custom_background(Color{r,g,b,a})`
with a low `a` tints what is underneath instead of hiding it. `with_opacity`
also scales a colour's existing alpha rather than replacing it.

The diff drag-to-select highlight (`src/ui/diff_renderer.h`, `diff_sel_hl`) is
a plain translucent box over the already-rendered line — the opaque-box-plus-
re-drawn-substring workaround this entry described is gone.

---

### No Rich Text / Multi-Color Text in a Single Label — RESOLVED, and moot

`with_styled_label({{"M ", STATUS_MODIFIED}, {"theme.h ", TEXT_PRIMARY}, ...})`
is on `ComponentConfig` (`component_config.h:658`).

Deliberately not adopted: `render_file_row_impl` already solved this with three
sized child divs (status glyph, filename, dir), which is what gives each column
its own width and ellipsis. A styled label would trade that away to save two
entities. The workaround described below — baking everything into one string
and losing the coloured status letter — has not been the code for a while.

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

### `with_font_weight` — BLOCKED on font files, not on API

`with_font_weight` is on `ComponentConfig`. It looks up a font registered as
`"<font>@bold"` and falls back to the base font when there is none, which is
why this read as "no font weight support"; since `90f8ae8` that fallback warns
once instead of being silent.

Adopting it needs `Roboto-Bold.ttf` and `JetBrainsMono-Bold.ttf` in
`resources/fonts/` and two more `fontMgr.load_font(... "@bold")` calls in
`preload.cpp`. That is a licensing/asset decision, so it is left alone here.

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

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

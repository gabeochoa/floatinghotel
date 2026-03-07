# dump_ui E2E Command Design

Add a `dump_ui` command to the afterhours E2E testing framework that outputs an XML tree of the rendered UI hierarchy. Deterministic, diffable, and doesn't depend on `screencapture`.

---

## Motivation

- Screenshots via macOS `screencapture` are flaky ("could not create image from window")
- No way to programmatically assert component position, size, or nesting
- XML is diffable in tests and readable by AI agents without vision

## Usage

```
dump_ui commit_area_state
```

Writes `commit_area_state.xml` to the screenshot output directory. Example output:

```xml
<ui name="sidebar_root" x="0" y="64" w="280" h="656">
  <ui name="commit_area" x="0" y="92" w="280" h="82">
    <ui name="commit_hint" x="8" y="96" w="264" h="16" text="Message (Enter to commit on &quot;main&quot;)"/>
    <ui name="commit_msg_input" x="8" y="116" w="264" h="26"/>
    <ui name="commit_btn_inline" x="8" y="146" w="264" h="24" text="Commit"/>
  </ui>
  <ui name="staged_header" x="0" y="178" w="280" h="22" text="Staged Changes  1"/>
</ui>
```

## Location

`vendor/afterhours/src/plugins/e2e_testing/ui_commands.h` -- new `HandleDumpUICommand` struct alongside existing UI command handlers.

## Architecture

### Callback pattern

Same as `HandleScreenshotCommand`: the command handler takes a `std::function<void(const std::string& name, const std::string& xml)>` via constructor. The app (main.cpp) provides the callback that writes to disk. The library never touches the filesystem directly.

### Tree walk

1. Query all entities with `UIComponent` where `parent == -1` (root nodes)
2. Filter to `was_rendered_to_screen == true`
3. Recursively walk `UIComponent::children` IDs, resolving each via `UICollectionHolder::getEntityForID`
4. Skip nodes where `was_rendered_to_screen == false`

### Attributes per node

| Attribute | Source | Notes |
|-----------|--------|-------|
| `name` | `UIComponentDebug::name()` | Omitted if no debug name |
| `x`, `y`, `w`, `h` | `get_screen_rect()` (includes scroll offset + modifiers) | Integer-rounded |
| `text` | `HasLabel::label` | Only if entity has `HasLabel`, XML-escaped |
| `scroll_x`, `scroll_y` | `HasScrollView::scroll_offset` | Only if entity has `HasScrollView` |
| `hidden` | `UIComponent::should_hide` | Only emitted when `true` |

Nodes with no children use self-closing tags (`<ui .../>`). Nodes with children use open/close tags with indented children.

### Registration

Added inside `register_ui_commands<InputAction>()` with a callback parameter:

```cpp
template <typename InputAction>
void register_ui_commands(SystemManager &sm,
                          HandleDumpUICommand::DumpFn dump_fn = nullptr) {
    // ... existing registrations unchanged ...
    if (dump_fn) {
        sm.register_update_system(
            std::make_unique<HandleDumpUICommand>(std::move(dump_fn)));
    }
}
```

This is additive -- existing callers that don't pass a dump callback continue to compile and work with no changes.

### App-side wiring (main.cpp)

```cpp
afterhours::testing::ui_commands::register_ui_commands<InputAction>(
    sm,
    [](const std::string& name, const std::string& xml) {
        std::filesystem::path dir =
            std::filesystem::absolute(app_state::screenshotDir);
        std::filesystem::create_directories(dir);
        std::filesystem::path path = dir / (name + ".xml");
        std::ofstream out(path);
        out << xml;
        log_info("UI dump: {}", path.string());
    });
```

## What this does NOT change

- No existing command signatures change
- No existing `register_ui_commands` call sites break (new param has default)
- No new dependencies
- `HandleScreenshotCommand` and all other handlers are untouched

## Implementation estimate

One file changed in vendor (`ui_commands.h`), one file changed in app (`main.cpp`). ~80 lines of new code.

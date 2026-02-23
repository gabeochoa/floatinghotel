# Refactor 07: Singleton Query Helper

## Problem

Nearly every UI system starts with identical boilerplate to fetch singleton
components:

```cpp
auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
    .whereHasComponent<LayoutComponent>().gen();
if (layoutEntities.empty()) return;
auto& layout = layoutEntities[0].get().get<LayoutComponent>();

auto repoEntities = afterhours::EntityQuery({.force_merge = true})
    .whereHasComponent<RepoComponent>()
    .whereHasComponent<ActiveTab>().gen();
```

This 4-line pattern appears in:
- `LayoutUpdateSystem`
- `SidebarSystem`
- `ToolbarSystem`
- `StatusBarSystem`
- `MenuBarSystem`
- `MainContentSystem`
- `TabBarSystem` (for `TabStripComponent`)
- `render_command_log` (free function)
- Multiple lambdas in `menu_setup.h` (~12 occurrences)

That's 30+ occurrences of the same query-then-null-check pattern, totaling
~120 lines of boilerplate.

## Plan

### Option A: Lightweight query helper (recommended)

Add a small helper to `src/ecs/ui_imports.h` (or a new `query_helpers.h`):

```cpp
namespace ecs {

// Returns a pointer to the first entity's component, or nullptr if none found.
template<typename... Components>
auto* query_first() {
    auto results = afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<Components...>().gen();
    if (results.empty()) return static_cast<
        std::remove_reference_t<decltype(
            results[0].get().template get<
                std::tuple_element_t<0, std::tuple<Components...>>>())>*>(nullptr);
    return &results[0].get().template get<
        std::tuple_element_t<0, std::tuple<Components...>>>();
}

} // namespace ecs
```

Usage becomes:

```cpp
auto* layout = ecs::query_first<LayoutComponent>();
if (!layout) return;
```

One line instead of four.

### Option B: Cache singleton pointers

Since `LayoutComponent`, `MenuComponent`, `TabStripComponent`, and
`CommandLogComponent` live on entities created once at startup and never
destroyed, their pointers are stable for the lifetime of the app.

Cache them once in `app_init()`:

```cpp
namespace app_state {
    LayoutComponent* layout = nullptr;
    MenuComponent* menu = nullptr;
    TabStripComponent* tabStrip = nullptr;
    CommandLogComponent* cmdLog = nullptr;
}
```

Systems access `app_state::layout->` directly. No per-frame queries needed.

This is more performant (zero per-frame queries for singletons) but less
"ECS-pure". Acceptable for a single-threaded UI app.

### Option C: Both

Use Option A for `RepoComponent + ActiveTab` queries (which change when
tabs switch) and Option B for true singletons (layout, menu, tab strip).

## Risk

**Option A**: Very low. It's a thin wrapper around existing query code.

**Option B**: Low, but requires care if entities are ever destroyed and
recreated (currently they aren't). A dangling pointer would crash.

## Estimated impact

- ~120 lines of boilerplate reduced to ~30
- `menu_setup.h` lambdas become much more readable (each currently has
  a 4-line query block for a 1-line action)
- Per-frame performance: Option B eliminates ~30 ECS queries per frame
  for components that never change

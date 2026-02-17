# Design System: Component Presets

**Date:** 2026-02-17
**Status:** Approved, migrating incrementally

## Problem

Every UI component is styled inline with 8-12 builder calls. The same patterns (corner radius, font size, padding, colors) are copy-pasted across `sidebar_system.h`, `toolbar_system.h`, `status_bar_system.h`, `menu_bar_system.h`, and `diff_renderer.h`. This leads to:

- Inconsistent disabled states (3 different "disabled" looks for buttons)
- Easy to forget roundness/corners on a new component
- Tedious to change a design token globally
- ~20 button instances all manually setting the same 7 properties

## Solution

A single `src/ui/presets.h` file in a `preset` namespace. Each preset is an `inline` function returning a `ComponentConfig` with all design-system defaults baked in. Callers can still chain `.with_*()` to override.

## Design Decisions

1. **One `Button()` for everything.** No toolbar/dialog/primary variants as separate functions. The default is primary (blue). Callers chain `.with_custom_background()` for secondary/destructive.
2. **Disabled is baked in.** `Button(label, enabled)` with `enabled` defaulting to `true`. Disabled always means the same bg + text color everywhere.
3. **Size is baked in.** Each preset includes a sensible default size. Caller overrides with `.with_size()` when needed.
4. **Font tiers from afterhours.** Presets use `with_font_tier(FontSizing::Tier::X)` instead of raw pixel constants. The 8 `theme::layout::FONT_*` constants collapse to 4 tiers.
5. **Presets do NOT set:** `mk()` IDs, absolute position/translate, render_layer, debug_name. These are instance-specific.

## Presets

### Buttons
| Preset | Default Size | Behavior |
|--------|-------------|----------|
| `Button(label, enabled=true)` | `children() x h720(32)` | Primary bg, white text, rounded corners, center aligned. Disabled swaps to `DISABLED_BG`/`DISABLED_TEXT`. |

### Layout
| Preset | Default Size |
|--------|-------------|
| `SectionHeader(label)` | `percent(1.0f) x children()` |
| `SelectableRow(selected)` | `percent(1.0f) x h720(FILE_ROW_HEIGHT)` |
| `Badge(label, bg, text)` | `children() x children()` |
| `RowSeparator()` | `percent(1.0f) x pixels(1)` |
| `ScrollPanel()` | `percent(1.0f) x percent(1.0f)` |
| `DialogButtonRow()` | `percent(1.0f) x h720(44)` |

### Text
| Preset | Default Size | Font Tier |
|--------|-------------|-----------|
| `BodyText(label)` | `percent(1.0f) x children()` | XL |
| `MetaText(label)` | `children() x children()` | Medium |
| `EmptyStateText(label)` | `percent(1.0f) x children()` | Medium |
| `CaptionText(label)` | `children() x children()` | Small |
| `DialogMessage(text)` | `percent(1.0f) x children()` | Medium |

## Theme Tokens Added

```
theme::DISABLED_BG    — unified disabled button background
theme::DISABLED_TEXT  — unified disabled button text color
```

## Migration Plan

Each step is a pure refactor with zero expected visual changes (except step 9). After each step: rebuild, re-run E2E baseline script, pixel-compare with ImageMagick `compare`.

| Step | Scope | Expected Visual Change |
|------|-------|----------------------|
| 1 | Create `presets.h`, add theme tokens | None (additive only) |
| 2 | Migrate buttons | None |
| 3 | Migrate section headers | None |
| 4 | Migrate selectable rows | None |
| 5 | Migrate badges | None |
| 6 | Migrate text (body, meta, empty state) | None |
| 7 | Migrate dialog elements | None |
| 8 | Migrate layout containers | None |
| 9 | Migrate font constants to FontSizing tiers | **Yes — intentional** |

## Font Tier Mapping

Current constants collapse to:

| Tier | Mapped From | Value |
|------|------------|-------|
| Small | FONT_CAPTION (12), FONT_TOOLBAR (13) | 13 |
| Medium | FONT_CODE (24), FONT_META (24) | 24 |
| Large | FONT_HEADING (22), FONT_CHROME (26), FONT_HERO (26) | 26 |
| XL | FONT_BODY (28) | 28 |

Step 9 will cause FONT_CAPTION (12→13) and FONT_HEADING (22→26) to change size. These are intentional unifications.

## Comparison Process

```bash
# After each step:
make -j$(sysctl -n hw.ncpu)
./output/floatinghotel.exe --test-mode \
  --test-script=tests/e2e_scripts/baseline_all_flows.e2e \
  --screenshot-dir=output/screenshots/after

# Pixel-diff every pair:
for f in output/screenshots/baseline/*.png; do
  name=$(basename "$f")
  compare -metric AE "$f" "output/screenshots/after/$name" \
    "output/screenshots/diff/$name" 2>&1
done
```

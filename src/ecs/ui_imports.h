#pragma once

// Shared UI type aliases for ECS system headers.
// Include this at file scope (before any namespace block) to bring
// commonly-used afterhours UI types into the translation unit.

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../ui/presets.h"
#include "../ui/theme.h"
#include "../ui_context.h"
#include "components.h"

using afterhours::Entity;
using afterhours::EntityHelper;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::hstack;
using afterhours::ui::imm::button;
using afterhours::ui::imm::mk;
using afterhours::ui::imm::checkbox;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;
using afterhours::ui::expand;
using afterhours::ui::percent;
using afterhours::ui::children;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::JustifyContent;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::FontSize;
using afterhours::ui::HasClickListener;
using afterhours::ui::HasDragListener;
using afterhours::ui::ClickActivationMode;
using afterhours::ui::Overflow;
using afterhours::ui::Axis;
using afterhours::ui::resolve_to_pixels;

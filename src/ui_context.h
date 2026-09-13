#pragma once

#include "../vendor/afterhours/src/plugins/ui.h"
#include "../vendor/afterhours/src/plugins/window_manager.h"
#include "../vendor/afterhours/src/plugins/toast.h"
#include "../vendor/afterhours/src/plugins/modal.h"
#include "rl.h"
#include "input_mapping.h"
#include "ui/tooltip.h"
#include "ui/context_menu.h"

namespace ui_imm {

using InputAction = ::InputAction;
using UIContextType = afterhours::ui::UIContext<InputAction>;

// Initialize the UI context with screen dimensions and dark theme
inline void initUIContext(int screenWidth, int screenHeight) {
    using namespace afterhours;

    auto* resProv = EntityHelper::get_singleton_cmp<
        window_manager::ProvidesCurrentResolution>();
    if (resProv) {
        resProv->current_resolution = {screenWidth, screenHeight};
    }
}

// Get the root entity for parenting UI elements
inline afterhours::Entity& getUIRootEntity() {
    afterhours::OptEntity root =
        afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<afterhours::ui::AutoLayoutRoot>()
            .gen_first();
    if (!root) {
        throw std::runtime_error("No UI root found");
    }
    return root.asE();
}

struct ClearPendingUIDraws : afterhours::System<UIContextType> {
    void for_each_with(afterhours::Entity&, UIContextType& context, float) override {
        context.render_cmds.clear();
    }
};

struct ReserveContextMenuKeys : afterhours::System<UIContextType> {
    void for_each_with(afterhours::Entity&, UIContextType& context, float) override {
        if (!::ui::is_context_menu_open()) return;
        for (const auto action : {InputAction::WidgetUp, InputAction::WidgetDown,
                InputAction::WidgetLeft, InputAction::WidgetRight,
                InputAction::WidgetNext, InputAction::WidgetPress}) {
            static_cast<void>(context.pressed(action));
            const auto index = magic_enum::enum_index(action).value();
            context.all_actions_repeat[index] = false;
        }
    }
};

inline void registerUIPreLayoutSystems(
    afterhours::SystemManager& manager) {
    manager.register_update_system(std::make_unique<ClearPendingUIDraws>());
    afterhours::ui::register_before_ui_updates<InputAction>(manager);
    manager.register_update_system(std::make_unique<ReserveContextMenuKeys>());
}

struct HandleVisibleScrollInput : afterhours::ui::HandleScrollInput<InputAction> {
    void for_each_with(afterhours::Entity& entity, afterhours::ui::UIComponent& cmp,
                       afterhours::ui::HasScrollView& scroll, float dt) override {
        using namespace afterhours;
        using namespace afterhours::ui;
        if (!cmp.was_rendered_to_screen || cmp.should_hide || entity.has<ShouldHide>()) return;
        scroll.viewport_size = {cmp.computed[Axis::X], cmp.computed[Axis::Y]};
        if (!context->is_input_allowed(entity.id)) {
            scroll.scroll_target = scroll.last_eased_offset = scroll.scroll_offset;
            return;
        }
        scroll.ease_scroll(dt);
        if (scroll.auto_overflow && !(scroll.vertical_enabled && scroll.needs_scroll_y()) &&
            !(scroll.horizontal_enabled && scroll.needs_scroll_x())) {
            scroll.scroll_offset = scroll.scroll_target = {0, 0};
            return;
        }
        const auto rect = afterhours::ui::detail::apply_scroll_offset(entity, cmp.rect());
        const auto [clipped, clip] = afterhours::ui::detail::compute_intersected_clip_rect(entity);
        if (rect.width <= 0 || rect.height <= 0 || !is_mouse_inside(context->mouse.pos, rect) ||
            (clipped && !is_mouse_inside(context->mouse.pos, clip))) return;
        if (::ui::is_context_menu_open()) return;
        const auto wheel = input::get_mouse_wheel_move_v();
        const float direction = scroll.invert_scroll ? 1.f : -1.f;
        if (scroll.vertical_enabled) scroll.scroll_target.y += direction * wheel.y * scroll.scroll_speed;
        if (scroll.horizontal_enabled) scroll.scroll_target.x += direction * wheel.x * scroll.scroll_speed;
        scroll.clamp_scroll();
    }
};

struct HandleAllowedScrollbarDrag : afterhours::ui::HandleScrollbarDrag<InputAction> {
    void for_each_with(afterhours::Entity& entity, afterhours::ui::UIComponent& component,
            afterhours::ui::HasScrollView& scroll, float dt) override {
        if (context && !context->is_input_allowed(entity.id)) {
            scroll.dragging_scrollbar = false;
            return;
        }
        afterhours::ui::HandleScrollbarDrag<InputAction>::for_each_with(entity, component, scroll, dt);
    }
};

inline void registerUIPostLayoutSystems(
    afterhours::SystemManager& manager) {
    afterhours::ui::register_after_ui_updates<InputAction>(manager);
    for (auto& system : manager.update_systems_) {
        auto* bridge = dynamic_cast<afterhours::ui::UIPluginPostUpdateBridge<InputAction>*>(system.get());
        if (!bridge) continue;
        for (auto& child : bridge->systems)
            if (dynamic_cast<afterhours::ui::HandleScrollInput<InputAction>*>(child.get()))
                child = std::make_unique<HandleVisibleScrollInput>();
            else if (dynamic_cast<afterhours::ui::HandleScrollbarDrag<InputAction>*>(child.get()))
                child = std::make_unique<HandleAllowedScrollbarDrag>();
    }
}

struct HoldFloatingUIDraws : afterhours::System<UIContextType> {
    std::vector<afterhours::ui::RenderInfo> commands;

    void for_each_with(afterhours::Entity&, UIContextType& context, float) override {
        commands.clear();
        std::erase_if(context.render_cmds, [&](const auto& command) {
            if (command.layer < 100) return false;
            commands.push_back(command);
            return true;
        });
    }
};

struct RenderFloatingUI : afterhours::ui::RenderImm<InputAction> {
    HoldFloatingUIDraws& held;

    explicit RenderFloatingUI(HoldFloatingUIDraws& draws) : held(draws) {}

    void for_each_with_derived(afterhours::Entity& entity, UIContextType& context,
            afterhours::ui::FontManager& fonts, float dt) override {
        if (held.commands.empty()) return;
        context.render_cmds.swap(held.commands);
        afterhours::ui::RenderImm<InputAction>::for_each_with_derived(entity, context, fonts, dt);
    }
};

struct RenderFloatingScrollbars : afterhours::ui::RenderScrollbars<InputAction> {
    void for_each_with(afterhours::Entity& entity, afterhours::ui::UIComponent& component,
            afterhours::ui::HasScrollView& scroll, float dt) override {
        if (component.render_layer >= 100)
            afterhours::ui::RenderScrollbars<InputAction>::for_each_with(entity, component, scroll, dt);
    }
};

inline void registerUIRenderSystems(
    afterhours::SystemManager& manager) {
    auto bridge = std::make_unique<afterhours::ui::UIPluginRenderBridge<InputAction>>(InputAction::None, false);
    auto held = std::make_unique<HoldFloatingUIDraws>();
    auto floating = std::make_unique<RenderFloatingUI>(*held);
    bridge->systems.insert(bridge->systems.begin(), std::move(held));
    bridge->systems.insert(bridge->systems.begin() + 3, std::move(floating));
    bridge->systems.insert(bridge->systems.begin() + 4, std::make_unique<RenderFloatingScrollbars>());
    manager.register_render_system(std::move(bridge));
    manager.register_render_system(std::make_unique<ui::RenderWrappedTooltip<InputAction>>());
}

inline void registerModalSystems(
    afterhours::SystemManager& manager) {
    afterhours::modal::enforce_singletons(manager);
    afterhours::modal::register_update_systems<InputAction>(manager);
}

inline void registerModalRenderSystems(
    afterhours::SystemManager& manager) {
    afterhours::modal::register_render_systems<InputAction>(manager);
}

}  // namespace ui_imm

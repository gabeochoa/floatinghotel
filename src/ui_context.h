#pragma once

#include "../vendor/afterhours/src/plugins/ui.h"
#include "../vendor/afterhours/src/plugins/window_manager.h"
#include "../vendor/afterhours/src/plugins/toast.h"
#include "../vendor/afterhours/src/plugins/modal.h"
#include "rl.h"
#include "input_mapping.h"
#include "ui/tooltip.h"

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

inline void registerUIPreLayoutSystems(
    afterhours::SystemManager& manager) {
    manager.register_update_system(std::make_unique<ClearPendingUIDraws>());
    afterhours::ui::register_before_ui_updates<InputAction>(manager);
}

inline void registerUIPostLayoutSystems(
    afterhours::SystemManager& manager) {
    afterhours::ui::register_after_ui_updates<InputAction>(manager);
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

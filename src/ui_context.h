#pragma once

#include <afterhours/src/plugins/ui.h>
#include <afterhours/src/plugins/window_manager.h>
#include <afterhours/src/plugins/toast.h>
#include <afterhours/src/plugins/modal.h>
#include "rl.h"
#include "input_mapping.h"
#include "ui/tooltip.h"
#include "ui/sticky_file_header.h"
#include "util/reading_load.h"
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

struct ResetReadingLoadFrame : afterhours::System<UIContextType> {
    void for_each_with(afterhours::Entity&, UIContextType&, float) override {
        reading_load::frame = {};
    }
};

struct ReserveContextMenuInput : afterhours::System<UIContextType> {
    void for_each_with(afterhours::Entity&, UIContextType& context, float) override {
        context.remove_input_gate("context-menu");
        if (!::ui::is_context_menu_open()) return;
        context.add_input_gate("context-menu", [](afterhours::EntityID id) {
            auto entity = afterhours::ui::UICollectionHolder::getEntityForID(id);
            return entity.valid() && entity->has<afterhours::ui::UIComponentDebug>() &&
                entity->get<afterhours::ui::UIComponentDebug>().name().starts_with("context_menu");
        });
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
    manager.register_update_system(std::make_unique<ResetReadingLoadFrame>());
    afterhours::ui::register_before_ui_updates<InputAction>(manager);
    manager.register_update_system(std::make_unique<ReserveContextMenuInput>());
}

struct TimedAutoLayout : afterhours::ui::RunAutoLayout {
    void for_each_with(afterhours::Entity& entity, afterhours::ui::AutoLayoutRoot& root,
                       afterhours::ui::UIComponent& component, float dt) override {
        const auto start = reading_load::Clock::now();
        afterhours::ui::RunAutoLayout::for_each_with(entity, root, component, dt);
        const auto ms = reading_load::milliseconds(reading_load::Clock::now() - start);
        reading_load::frame.layoutMs += ms;
        reading_load::totalLayoutMs += ms;
    }
};

inline void registerUIPostLayoutSystems(
    afterhours::SystemManager& manager) {
    afterhours::ui::register_after_ui_updates<InputAction>(manager);
    manager.register_update_system(std::make_unique<::ui::FilterTruncatedTooltips<InputAction>>());
    for (auto& system : manager.update_systems_) {
        auto* bridge = dynamic_cast<afterhours::ui::UIPluginPostUpdateBridge<InputAction>*>(system.get());
        if (!bridge) continue;
        for (size_t i = 0; i < bridge->systems.size(); ++i) {
            if (dynamic_cast<afterhours::ui::RunAutoLayout*>(bridge->systems[i].get())) {
                bridge->systems.insert(bridge->systems.begin() + static_cast<std::ptrdiff_t>(i),
                    std::make_unique<::ui::ResetStickyFileHeaders>());
                ++i;
            } else if (dynamic_cast<afterhours::ui::MeasureScrollViews*>(bridge->systems[i].get()) ||
                       dynamic_cast<afterhours::ui::SyncScrollViews*>(bridge->systems[i].get())) {
                bridge->systems.insert(bridge->systems.begin() + static_cast<std::ptrdiff_t>(++i),
                    std::make_unique<::ui::PinFileHeaders>());
            }
        }
        for (auto& child : bridge->systems)
            if (dynamic_cast<afterhours::ui::RunAutoLayout*>(child.get()))
                child = std::make_unique<TimedAutoLayout>();
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

inline void registerUIRenderSystems(
    afterhours::SystemManager& manager) {
    auto bridge = std::make_unique<afterhours::ui::UIPluginRenderBridge<InputAction>>(InputAction::None, false);
    auto held = std::make_unique<HoldFloatingUIDraws>();
    auto floating = std::make_unique<RenderFloatingUI>(*held);
    // Bridge order is [RenderImm, debug overlay]: the renderer draws each
    // layer's scrollbars itself (afterhours 6931c61), so floating UI and its
    // scrollbars render after the main pass and before the debug overlay.
    bridge->systems.insert(bridge->systems.begin(), std::move(held));
    bridge->systems.insert(bridge->systems.begin() + 2, std::move(floating));
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

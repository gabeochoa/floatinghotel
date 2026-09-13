#pragma once

#include "../ecs/ui_imports.h"
#include "../util/document_cycle.h"
#include "../util/document_titles.h"
#include "../util/navigation.h"
#include "context_menu.h"
#include "zoom.h"

namespace ui {

struct RecentTabSwitcher : afterhours::BaseComponent {
    int repository = -1;
    std::optional<reading::DocumentCycle> cycle;
};

inline bool render_document_switcher(afterhours::ui::UIContext<InputAction>& ctx, ecs::LayoutComponent& layout,
                                    ecs::RepoComponent* repo, bool blocked) {
    using namespace ecs;
    auto& root = ui_imm::getUIRootEntity();
    auto& state = root.addComponentIfMissing<RecentTabSwitcher>();
    auto* owner = find_singleton_entity<RepoComponent, ActiveTab>();
    const bool ctrl = afterhours::input::is_key_down(341) || afterhours::input::is_key_down(345);
    const bool step = ctrl && afterhours::input::is_key_pressed(258);
    const bool wasOpen = state.cycle.has_value();
    ctx.remove_input_gate("document-switcher");
    if (wasOpen) ctx.add_input_gate("document-switcher", [](int) { return false; });
    if (!repo || !owner || (wasOpen && state.repository != owner->id)) {
        state.cycle.reset();
        return wasOpen;
    }
    if (wasOpen) state.cycle->retain(reading::recent_documents(repo->workspace()));
    if (wasOpen && (blocked || afterhours::input::is_key_pressed(afterhours::keys::ESCAPE) ||
                    ctx.mouse.just_pressed || !state.cycle->selected())) {
        state.cycle.reset();
        return true;
    }
    if (step && !blocked && !layout.filePickerOpen && !is_context_menu_open()) {
        static_cast<void>(ctx.pressed(InputAction::WidgetNext));
        if (!state.cycle) state.cycle = reading::DocumentCycle{reading::recent_documents(repo->workspace())};
        state.repository = owner->id;
        if (state.cycle->order.size() < 2) { state.cycle.reset(); return true; }
        const bool shift = afterhours::input::is_key_down(340) || afterhours::input::is_key_down(344);
        state.cycle->advance(shift ? -1 : 1);
    }
    if (!state.cycle) return false;
    if (!ctrl) {
        if (const auto selected = state.cycle->selected()) navigation::activate(*repo, *selected);
        state.cycle.reset();
        return true;
    }
    ctx.add_input_gate("document-switcher", [](int) { return false; });
    const auto& cycle = *state.cycle;
    const float scale = zoom::get();
    const float screenWidth = afterhours::graphics::get_screen_width() / scale;
    const float screenHeight = afterhours::graphics::get_screen_height() / scale;
    const float width = std::max(1.f, std::min(520.f, screenWidth - 24.f));
    const auto rows = std::min(cycle.order.size(), static_cast<size_t>(std::max(1.f, std::min(8.f, (screenHeight - 88.f) / 30.f))));
    const float height = 40.f + static_cast<float>(rows) * 30.f;
    auto panel = div(ctx, mk(root, 595800), ComponentConfig{}
        .with_size(ComponentSize{pixels(width), pixels(height)})
        .with_absolute_position((screenWidth - width) * .5f, std::max(0.f, (screenHeight - height) * .35f))
        .with_flex_direction(FlexDirection::Column).with_no_wrap().with_padding(Padding{.left = pixels(8), .right = pixels(8)})
        .with_custom_background(theme::SIDEBAR_BG).with_border(theme::BORDER, pixels(1))
        .with_roundness(0.f).with_corner_radius(6.f)
        .with_render_layer(1000).with_debug_name("recent_tabs"));
    div(ctx, mk(panel.ent(), 0), ComponentConfig{}
        .with_label("Recent tabs · " + std::to_string(cycle.index + 1) + " of " + std::to_string(cycle.order.size()))
        .with_size(ComponentSize{percent(1.f), pixels(40)}).with_font_size(pixels(13))
        .with_custom_text_color(theme::TEXT_SECONDARY).with_roundness(0.f).with_render_layer(1001).with_debug_name("recent_tabs_position"));
    const auto titles = reading::document_titles(repo->workspace().documents());
    const size_t start = std::min(cycle.index > rows / 2 ? cycle.index - rows / 2 : 0, cycle.order.size() - rows);
    for (size_t i = start; i < start + rows; ++i) {
        const auto id = cycle.order[i];
        const auto& documents = repo->workspace().documents();
        const auto found = std::find_if(documents.begin(), documents.end(), [&](const auto& document) { return document.id == id; });
        const auto& title = titles[static_cast<size_t>(found - documents.begin())];
        div(ctx, mk(panel.ent(), static_cast<int>(id.value) + 1), ComponentConfig{}
            .with_label(title.label + (title.badge.empty() ? "" : " · " + title.badge))
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(pixels(14)).with_text_inset(8.f)
            .with_custom_background(i == cycle.index ? theme::SELECTED_BG : theme::SIDEBAR_BG)
            .with_custom_text_color(theme::TEXT_PRIMARY).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_roundness(0.f).with_corner_radius(4.f).with_render_layer(1001).with_debug_name(std::string(i == cycle.index ? "recent_tab_selected_" : "recent_tab_") + std::to_string(id.value)));
    }
    return true;
}

}

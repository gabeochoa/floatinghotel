#pragma once

#include "../ecs/ui_imports.h"
#include "../util/fuzzy_match.h"
#include "focus.h"
#include "virtual_list.h"
#include <afterhours/src/plugins/modal.h>

namespace ecs {

inline void render_file_picker(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, LayoutComponent& layout, float height) {
    bool revealSelection = layout.filePickerFocus;
    ui::bind_focus(parent, repo, reading::focus::Region::Picker);
    div(ctx, mk(parent, 586000), ComponentConfig{}
        .with_label("Go to file · working tree")
        .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(pixels(14)));
    auto input = afterhours::text_input::text_input(ctx, mk(parent, 586001), layout.filePickerQuery,
        ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)}).with_debug_name("file_picker_input"));
    if (layout.filePickerFocus || ui::shortcut_owner(ctx, repo).region != reading::focus::Region::Picker) {
        ui::focus_control(ctx, input.ent());
        layout.filePickerFocus = false;
    }
    std::string key = repo.repoPath + ":" + std::to_string(repo.dataGeneration) + ":" +
                      std::to_string(repo.repoVersion) + "\n" + layout.filePickerQuery;
    if (key != layout.filePickerCacheKey) {
        layout.filePickerResults = fuzzy::rank(repo.allFilePaths, layout.filePickerQuery);
        layout.filePickerCacheKey = key;
        layout.filePickerIndex = 0;
        revealSelection = true;
    }
    auto open = [&](const std::string& path, bool keep) {
        navigation::click(repo, reading::source(path), keep, reading::ClickRegion::Picker);
    };
    auto& results = layout.filePickerResults;
    const bool pickerKeys = !ui::shortcuts_blocked(layout) && ui::shortcut_owner(ctx, repo).input(reading::focus::Region::Picker);
    if (pickerKeys && !results.empty()) {
        if (afterhours::input::is_key_pressed(264)) layout.filePickerIndex = std::min(layout.filePickerIndex + 1, static_cast<int>(results.size()) - 1);
        if (afterhours::input::is_key_pressed(265)) layout.filePickerIndex = std::max(0, layout.filePickerIndex - 1);
        if (afterhours::input::is_key_pressed(257)) open(results[layout.filePickerIndex], true);
    }
    div(ctx, mk(parent, 586002), ComponentConfig{}
        .with_label(repo.filesError.empty() ? std::to_string(results.size()) + " matches · arrows to choose · Enter open · Esc close" : repo.filesError)
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis));
    const auto listParent = mk(parent, 586003);
    if (revealSelection || (pickerKeys && (afterhours::input::is_key_pressed(264) || afterhours::input::is_key_pressed(265)))) {
        auto [entity, owner] = afterhours::ui::imm::deref(listParent);
        if (entity.has<afterhours::ui::HasScrollView>()) {
            auto& scroll = entity.get<afterhours::ui::HasScrollView>();
            const float rowHeight = 28.f * ui::zoom::get();
            const float top = static_cast<float>(layout.filePickerIndex) * rowHeight;
            const float viewport = std::max(28.f, height - 90.f) * ui::zoom::get();
            float target = scroll.scroll_offset.y;
            if (top < target) target = top;
            else if (top + rowHeight > target + viewport) target = top + rowHeight - viewport;
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = std::max(0.f, target);
            scroll.anchor_child = -1;
        }
        ui::focus_control(ctx, input.ent());
    }
    ui::virtual_list(ctx, listParent, results.size(), 28.f,
        [&](size_t i, Entity& row) {
            if (button(ctx, mk(row, 0), preset::Button(results[i])
                    .with_size(ComponentSize{percent(1.f), pixels(28)})
                    .with_alignment(TextAlignment::Left)
                    .with_custom_background(static_cast<int>(i) == layout.filePickerIndex ? theme::BUTTON_PRIMARY : theme::PANEL_BG)
                    .with_font_size(pixels(14)).with_debug_name("file_picker_result"))) open(results[i], false);
        }, ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(std::max(28.f, height - 90.f))})
            .with_debug_name("file_picker_list"));
}

struct FilePickerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        if (!layout) return;
        if (!repo || repo->repoPath.empty()) layout->filePickerOpen = false;
        auto& root = ui_imm::getUIRootEntity();
        const float zoom = ui::zoom::get();
        const float screenWidth = ctx.screen_width / zoom;
        const float screenHeight = ctx.screen_height / zoom;
        const float width = std::max(100.f, std::min(620.f, screenWidth - 32.f));
        const float height = std::max(160.f, std::min(440.f, screenHeight - 80.f));
        auto modal = afterhours::modal::detail::modal_impl(ctx, mk(root, 586100), layout->filePickerOpen,
            afterhours::ModalConfig{}.with_size(pixels(width), pixels(height))
                .with_show_close_button(false).with_closed_by(afterhours::ClosedBy::Any)
                .with_backdrop_color({0, 0, 0, 0}));
        modal.ent().get<afterhours::modal::Modal>().previously_focused_element = -1;
        if (!modal || !repo) {
            ctx.remove_input_gate("file_picker");
            return;
        }
        modal.cmp().absolute_pos_x = (ctx.screen_width - width * zoom) * .5f;
        modal.cmp().absolute_pos_y = (ctx.screen_height - height * zoom) * .3f;
        div(ctx, mk(root, 586101), ComponentConfig{}
            .with_size(ComponentSize{pixels(screenWidth), pixels(screenHeight)})
            .with_absolute_position().with_custom_background(afterhours::Color{0, 0, 0, 64})
            .with_roundness(0.f).with_render_layer(998).with_debug_name("file_picker_backdrop"));
        auto body = div(ctx, mk(modal.ent(), 586102), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_no_wrap()
            .with_render_layer(1001).with_debug_name("file_picker_overlay"));
        const auto modalId = modal.ent().id;
        ctx.add_input_gate("file_picker", [modalId](afterhours::EntityID id) {
            return afterhours::modal::detail::is_entity_in_tree(modalId, id);
        });
        render_file_picker(ctx, body.ent(), *repo, *layout, height - 48.f);
    }
};

}

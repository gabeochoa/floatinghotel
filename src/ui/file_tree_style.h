#pragma once

#include "../ecs/ui_imports.h"
#include "../util/file_tree.h"
#include "chrome_icons.h"
#include "tooltip.h"
#include "tree_keyboard.h"

namespace ui::file_tree_style {

inline std::string type_marker(const std::string& path) {
    const auto dot = path.find_last_of('.');
    const auto extension = dot == std::string::npos ? "" : path.substr(dot + 1);
    if (extension == "h" || extension == "hpp") return "H";
    if (extension == "cpp" || extension == "cc" || extension == "cxx") return "C++";
    if (extension == "ts" || extension == "tsx") return "TS";
    if (extension == "js" || extension == "jsx") return "JS";
    if (extension == "py") return "PY";
    if (extension == "md") return "MD";
    if (extension == "json") return "{}";
    if (extension == "html" || extension == "xml") return "<>";
    return "·";
}

inline auto row_config(float width, size_t depth, bool selected) {
    using namespace afterhours::ui;
    return preset::SelectableRow(selected)
        .with_consumes_directional_input()
        .with_size(ComponentSize{pixels(std::max(0.f, width - 16.f)), pixels(28)})
        .with_margin(Margin{.left = pixels(8), .right = pixels(8)})
        .with_padding(Padding{.top = pixels(0), .right = pixels(8), .bottom = pixels(0),
            .left = pixels(8.f + static_cast<float>(depth) * 12.f)})
        .with_gap(pixels(4)).with_overflow(Overflow::Hidden)
        .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(5.f);
}

inline void review_indicator(UIContext<InputAction>& ctx, Entity& row, bool reviewed, size_t unresolved, bool changed) {
    const std::string state = unresolved ? "unresolved" : changed ? "changed" : reviewed ? "reviewed" : "unreviewed";
    const std::string tooltip = unresolved ? std::to_string(unresolved) + " unresolved comments" :
        changed ? "Changed since last viewed" : reviewed ? "Reviewed" : "Not reviewed";
    auto status = div(ctx, mk(row, 96), ComponentConfig{}
        .with_size(ComponentSize{pixels(20), pixels(28)})
        .with_align_items(AlignItems::Center).with_justify_content(JustifyContent::Center)
        .with_debug_name("tree_review_status:" + state));
    set_tooltip(status.ent(), tooltip);
    if (unresolved) {
        div(ctx, mk(status.ent(), 0), ComponentConfig{}
            .with_label(unresolved > 99 ? "99+" : std::to_string(unresolved))
            .with_size(ComponentSize{pixels(20), pixels(20)}).with_font("mono", pixels(10))
            .with_custom_text_color(theme::STATUS_MODIFIED).with_debug_name("tree_unresolved_count"));
    } else if (reviewed && !changed) {
        chrome_icon(ctx, mk(status.ent(), 0), ChromeIcon::Check, theme::DIFF_ADD_TEXT, "tree_reviewed_check");
    } else {
        div(ctx, mk(status.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{pixels(7), pixels(7)})
            .with_border(changed ? theme::TEXT_ACCENT : theme::TEXT_TERTIARY, pixels(1))
            .with_custom_background(changed ? theme::TEXT_ACCENT : afterhours::Color{0, 0, 0, 0})
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(3.5f)
            .with_debug_name(changed ? "tree_changed_dot" : "tree_unreviewed_ring"));
    }
}

inline bool directory(afterhours::ui::UIContext<InputAction>& ctx, afterhours::Entity& parent,
                      const file_tree::Row& node, float width, bool collapsed, const std::string& name,
                      const ecs::RepoComponent& repo, file_tree::NavigationState& state) {
    using namespace afterhours::ui;
    using namespace afterhours::ui::imm;
    auto row = button(ctx, mk(parent, 0), row_config(width, node.depth, false)
        .with_label("").with_debug_name(name));
    bind_tree_row(ctx, row.ent(), repo, state, node.path);
    auto disclosure = div(ctx, mk(row.ent(), 0), ComponentConfig{}
        .with_size(ComponentSize{pixels(16), pixels(28)})
        .with_debug_name("tree_disclosure"));
    auto arrow = div(ctx, mk(disclosure.ent(), 0), ComponentConfig{}
        .with_size(ComponentSize{pixels(5), pixels(5)}).with_absolute_position(5.f, 10.f)
        .with_border_bottom(theme::TEXT_SECONDARY, pixels(1))
        .with_border_right(theme::TEXT_SECONDARY, pixels(1)).with_roundness(0.f));
    arrow.ent().addComponentIfMissing<HasUIModifiers>().rotation = collapsed ? -45.f : 45.f;
    auto icon = div(ctx, mk(row.ent(), 1), ComponentConfig{}
        .with_size(ComponentSize{pixels(16), pixels(16)}).with_debug_name("tree_folder_icon"));
    div(ctx, mk(icon.ent(), 0), ComponentConfig{}
        .with_size(ComponentSize{pixels(13), pixels(9)}).with_absolute_position(1.f, 5.f)
        .with_border(theme::TEXT_SECONDARY, pixels(1)).with_roundness(0.f));
    div(ctx, mk(icon.ent(), 1), ComponentConfig{}
        .with_size(ComponentSize{pixels(6), pixels(3)}).with_absolute_position(1.f, 3.f)
        .with_border_top(theme::TEXT_SECONDARY, pixels(1))
        .with_border_left(theme::TEXT_SECONDARY, pixels(1)));
    auto path = node.path.substr(0, node.path.size() - 1);
    auto slash = path.find_last_of('/');
    div(ctx, mk(row.ent(), 2), ComponentConfig{}
        .with_label(slash == std::string::npos ? path : path.substr(slash + 1))
        .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(13))
        .with_custom_text_color(theme::TEXT_SECONDARY)
        .with_text_overflow(TextOverflow::Ellipsis).with_debug_name("tree_directory_name"));
    return static_cast<bool>(row);
}

}

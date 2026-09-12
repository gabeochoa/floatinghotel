#pragma once

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <afterhours/src/plugins/e2e_testing/ui_commands.h>
#include <afterhours/src/plugins/ui/systems.h>
#include "zoom.h"
#include "diff_renderer.h"
#include "../input_mapping.h"

namespace ui {

inline RectangleType screen_rect(afterhours::Entity& entity) {
    using namespace afterhours::ui;
    auto rect = entity.get<UIComponent>().rect();
    if (entity.has<HasUIModifiers>()) rect = entity.get<HasUIModifiers>().apply_modifier(rect);
    return detail::apply_scroll_offset(entity, rect);
}

inline RectangleType visible_rect(afterhours::Entity& entity) {
    auto rect = screen_rect(entity);
    const auto [clipped, clip] = afterhours::ui::detail::compute_intersected_clip_rect(entity);
    if (clipped) rect = afterhours::ui::detail::intersect_rects(rect, clip);
    return afterhours::ui::detail::intersect_rects(rect, {0.f, 0.f,
        static_cast<float>(afterhours::graphics::get_screen_width()),
        static_cast<float>(afterhours::graphics::get_screen_height())});
}

inline nlohmann::json layout_snapshot() {
    using namespace afterhours;
    using namespace afterhours::ui;
    auto rect_json = [](const RectangleType& rect) {
        return nlohmann::json{{"x", rect.x}, {"y", rect.y}, {"width", rect.width}, {"height", rect.height}};
    };
    auto edges_json = [](const auto& edges) {
        return nlohmann::json{{"top", edges[Axis::top]}, {"right", edges[Axis::right]},
                              {"bottom", edges[Axis::bottom]}, {"left", edges[Axis::left]}};
    };
    auto* context = EntityHelper::get_singleton_cmp<UIContext<InputAction>>();
    auto nodes = nlohmann::json::array();
    for (Entity& entity : EntityQuery<>(UICollectionHolder::get().collection,
             {.force_merge = true, .ignore_temp_warning = true}).whereHasComponent<UIComponent>().gen()) {
        const auto& cmp = entity.get<UIComponent>();
        const auto rect = screen_rect(entity);
        nlohmann::json node{
            {"id", entity.id}, {"hot", context && context->is_hot(entity.id)}, {"parent", cmp.parent}, {"children", cmp.children},
            {"rendered", cmp.was_rendered_to_screen}, {"hidden", cmp.should_hide},
            {"rect", rect_json(rect)}, {"visible_rect", rect_json(visible_rect(entity))},
            {"padding", edges_json(cmp.computed_padd)},
            {"margin", edges_json(cmp.computed_margin)}, {"gap", cmp.gap},
            {"absolute", cmp.absolute},
            {"direction", std::string(magic_enum::enum_name(cmp.flex_direction))},
            {"justify", std::string(magic_enum::enum_name(cmp.justify_content))},
            {"align", std::string(magic_enum::enum_name(cmp.align_items))},
            {"font_size", {{"value", cmp.font_size.value}, {"unit", std::string(magic_enum::enum_name(cmp.font_size.dim))}}}
        };
        if (entity.has<UIComponentDebug>()) node["name"] = entity.get<UIComponentDebug>().name();
        if (entity.has<HasLabel>()) {
            const auto& label = entity.get<HasLabel>();
            node["text"] = label.label;
            if (entity.has<UIComponentDebug>() &&
                (entity.get<UIComponentDebug>().name() == "diff_line" || entity.get<UIComponentDebug>().name() == "sbs_cell")) {
                node["measured_text_width"] = EntityHelper::get_singleton_cmp_enforce<TextMeasureCache>().measure_width(
                    label.label, cmp.font_name, Settings::get().get_code_font_size() * zoom::get());
            }
            node["font"] = cmp.font_name;
            node["text_spans"] = nlohmann::json::array();
            for (const auto& span : label.spans)
                node["text_spans"].push_back({{"text", span.text},
                    {"color", {span.color.r, span.color.g, span.color.b, span.color.a}}});
            node["text_alignment"] = std::string(magic_enum::enum_name(label.alignment));
            node["text_overflow"] = std::string(magic_enum::enum_name(label.text_overflow));
            if (label.explicit_text_color) {
                const auto color = *label.explicit_text_color;
                node["text_color"] = {color.r, color.g, color.b, color.a};
            }
            if (label.text_inset) node["text_inset"] = {{"x", label.text_inset->x}, {"y", label.text_inset->y}};
        }
        if (entity.has<HasScrollView>()) {
            const auto& scroll = entity.get<HasScrollView>();
            node["scroll"] = {{"x", scroll.scroll_offset.x}, {"y", scroll.scroll_offset.y},
                               {"content_width", scroll.content_size.x}, {"content_height", scroll.content_size.y}};
        }
        nodes.push_back(std::move(node));
    }
    auto reading = nlohmann::json::array();
    for (const auto& row : diff_sel::state().lastLines) {
        reading.push_back({{"id", row.ent}, {"text", row.content}, {"path", row.filePath},
            {"line", row.lineNo}, {"side", row.side}, {"sign", std::string(1, row.sign)},
            {"offset", row.sourceOffset}, {"rect", rect_json(row.rect)}, {"content_x", row.contentX0}});
    }
    return {{"reading_rows", std::move(reading)}, {"selection_text", diff_sel::build_copy_text(diff_sel::state(), false)},
            {"selection_location", diff_sel::build_copy_text(diff_sel::state(), true)}, {"schema_version", 1}, {"units", "physical_pixels"}, {"ui_scale", zoom::get()},
            {"viewport", {{"width", graphics::get_screen_width()}, {"height", graphics::get_screen_height()}}},
            {"pointer", {{"x", context ? context->mouse.pos.x : 0.f}, {"y", context ? context->mouse.pos.y : 0.f}}},
            {"nodes", std::move(nodes)}};
}

inline void write_layout_snapshot(const std::filesystem::path& path) {
    std::ofstream output(path);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << layout_snapshot().dump(2, ' ', false, nlohmann::json::error_handler_t::replace) << '\n';
}

struct HandleDumpLayout : afterhours::System<afterhours::testing::PendingE2ECommand> {
    std::filesystem::path directory;

    explicit HandleDumpLayout(std::filesystem::path outputDirectory) : directory(std::move(outputDirectory)) {}

    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        if (cmd.is_consumed() || !cmd.is("dump_ui_json")) return;
        if (cmd.args.size() != 1 || cmd.args[0].empty() ||
            cmd.args[0].find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != std::string::npos) {
            cmd.fail("dump_ui_json requires a filename containing letters, digits, underscores, or hyphens");
            return;
        }
        try {
            std::filesystem::create_directories(directory);
            write_layout_snapshot(directory / (cmd.args[0] + ".json"));
            cmd.consume();
        } catch (const std::exception& error) {
            cmd.fail(std::string("Unable to write UI layout: ") + error.what());
        }
    }
};

struct HandleLayoutClick : afterhours::System<afterhours::testing::PendingE2ECommand> {
    void for_each_with(afterhours::Entity&, afterhours::testing::PendingE2ECommand& cmd, float) override {
        using namespace afterhours;
        using namespace afterhours::ui;
        if (cmd.is_consumed()) return;
        const bool named = cmd.is("click_ui") || cmd.is("right_click_ui") || cmd.is("hover_ui");
        const bool text = cmd.is("click_text") || cmd.is("right_click_text") || cmd.is("click_button");
        if (!named && !text) return;
        if (!cmd.has_args(1)) { cmd.fail("Click requires a UI name or label"); return; }
        for (Entity& entity : EntityQuery<>(UICollectionHolder::get().collection,
                {.force_merge = true, .ignore_temp_warning = true}).whereHasComponent<UIComponent>().gen()) {
            if (!entity.get<UIComponent>().was_rendered_to_screen) continue;
            if (named && (!entity.has<UIComponentDebug>() || entity.get<UIComponentDebug>().name() != cmd.arg(0))) continue;
            if (text && (!entity.has<HasLabel>() || entity.get<HasLabel>().label.find(cmd.arg(0)) == std::string::npos)) continue;
            if (cmd.is("click_button") && !entity.has<HasClickListener>()) continue;
            auto rect = visible_rect(entity);
            if (rect.width <= 0.f || rect.height <= 0.f) continue;
            const float x = rect.x + rect.width * 0.5f;
            const float y = rect.y + rect.height * 0.5f;
            if (cmd.is("hover_ui")) testing::test_input::set_mouse_position(x, y);
            else if (cmd.is("right_click_ui") || cmd.is("right_click_text")) testing::test_input::simulate_right_click(x, y);
            else testing::test_input::simulate_click(x, y);
            cmd.consume();
            return;
        }
        cmd.fail("Visible click target not found: " + cmd.arg(0));
    }
};

}

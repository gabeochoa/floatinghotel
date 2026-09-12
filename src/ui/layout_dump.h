#pragma once

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <afterhours/src/plugins/e2e_testing/ui_commands.h>
#include "zoom.h"

namespace ui {

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
    auto nodes = nlohmann::json::array();
    for (Entity& entity : EntityQuery<>(UICollectionHolder::get().collection,
             {.force_merge = true, .ignore_temp_warning = true}).whereHasComponent<UIComponent>().gen()) {
        const auto& cmp = entity.get<UIComponent>();
        const auto rect = testing::ui_commands::get_screen_rect(entity);
        nlohmann::json node{
            {"id", entity.id}, {"parent", cmp.parent}, {"children", cmp.children},
            {"rendered", cmp.was_rendered_to_screen}, {"hidden", cmp.should_hide},
            {"rect", rect_json(rect)}, {"padding", edges_json(cmp.computed_padd)},
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
            node["text_alignment"] = std::string(magic_enum::enum_name(label.alignment));
        }
        if (entity.has<HasScrollView>()) {
            const auto& scroll = entity.get<HasScrollView>();
            node["scroll"] = {{"x", scroll.scroll_offset.x}, {"y", scroll.scroll_offset.y},
                               {"content_width", scroll.content_size.x}, {"content_height", scroll.content_size.y}};
        }
        nodes.push_back(std::move(node));
    }
    return {{"schema_version", 1}, {"units", "physical_pixels"}, {"ui_scale", zoom::get()},
            {"viewport", {{"width", graphics::get_screen_width()}, {"height", graphics::get_screen_height()}}},
            {"nodes", std::move(nodes)}};
}

inline void write_layout_snapshot(const std::filesystem::path& path) {
    std::ofstream output(path);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output << layout_snapshot().dump(2) << '\n';
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

}

#pragma once

#include <ctime>
#include <string>

#include "../ecs/ui_imports.h"

namespace ecs {

inline std::string format_timestamp(double timestamp) {
    auto secs = static_cast<std::time_t>(timestamp);
    std::tm tm_buf{};
    localtime_r(&secs, &tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return std::string(buf);
}

inline void render_command_log(afterhours::ui::UIContext<InputAction>& ctx,
                               Entity& uiRoot,
                               LayoutComponent& layout) {
    auto* cmdLogPtr = find_singleton<CommandLogComponent>();
    if (!cmdLogPtr) return;
    auto& cmdLog = *cmdLogPtr;

    auto& cl = layout.commandLog;

    // === Horizontal divider at top of command log panel ===
    div(ctx, mk(uiRoot, 3200),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(cl.width), pixels(2)})
            .with_absolute_position()
            .with_translate(cl.x, cl.y)
            .with_custom_background(theme::BORDER)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_divider"));

    // Make divider draggable to resize the command log panel
    {
        auto dragDiv = div(ctx, mk(uiRoot, 3201),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(cl.width), pixels(6)})
                .with_absolute_position()
                .with_translate(cl.x, cl.y - 2.0f)
                .with_custom_background(afterhours::Color{0, 0, 0, 0})
                .with_roundness(0.0f)
                .with_debug_name("cmdlog_drag_handle"));

        dragDiv.ent().addComponentIfMissing<HasDragListener>(
            [](Entity& /*e*/) {});
        auto& drag = dragDiv.ent().get<HasDragListener>();
        if (drag.down) {
            auto mousePos = afterhours::graphics::get_mouse_position();
            float mouseY = static_cast<float>(mousePos.y);
            float bottomY = layout.statusBar.y;
            float topY = layout.mainContent.y + 60.0f;
            float newLogH = bottomY - mouseY;
            newLogH = std::clamp(newLogH, 80.0f, bottomY - topY);
            float sh = static_cast<float>(afterhours::graphics::get_screen_height());
            float unscaledLogH = newLogH * 720.0f / sh;

            auto* lc = find_singleton<LayoutComponent>();
            if (lc) lc->commandLogHeight = unscaledLogH;
        }
    }

    // === Panel background ===
    auto panelBg = div(ctx, mk(uiRoot, 3210),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(cl.width), pixels(cl.height)})
            .with_absolute_position()
            .with_translate(cl.x, cl.y)
            .with_custom_background(theme::SIDEBAR_BG)
            .with_flex_direction(FlexDirection::Column)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_panel"));

    // === Header bar ===
    constexpr float HEADER_H = 28.0f;
    auto headerBar = div(ctx, mk(panelBg.ent(), 3220),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), h720(HEADER_H)})
            .with_custom_background(theme::BORDER)
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_padding(Padding{
                .top = h720(0), .right = w1280(8),
                .bottom = h720(0), .left = w1280(8)})
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_header"));

    div(ctx, mk(headerBar.ent(), 3221),
        ComponentConfig{}
            .with_label("GIT COMMAND LOG")
            .with_size(ComponentSize{percent(1.0f), h720(HEADER_H)})
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_font_size(afterhours::ui::h720(16))
            .with_alignment(TextAlignment::Left)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_title"));

    std::string countLabel = std::to_string(cmdLog.entries.size()) + " commands";
    div(ctx, mk(headerBar.ent(), 3222),
        ComponentConfig{}
            .with_label(countLabel)
            .with_size(ComponentSize{children(), h720(HEADER_H)})
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_font_size(afterhours::ui::FontSize::Medium)
            .with_alignment(TextAlignment::Right)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_count"));

    // === Scrollable entries area ===
    float cmdLogScreenH = static_cast<float>(afterhours::graphics::get_screen_height());
    float cmdLogHeaderPx = resolve_to_pixels(h720(HEADER_H), cmdLogScreenH);
    float cmdLogScrollH = cl.height - cmdLogHeaderPx;
    if (cmdLogScrollH < 10.0f) cmdLogScrollH = 10.0f;
    auto scrollArea = div(ctx, mk(panelBg.ent(), 3230),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), pixels(cmdLogScrollH)})
            .with_overflow(Overflow::Auto, Axis::Y)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::SIDEBAR_BG)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_scroll"));

    if (cmdLog.entries.empty()) {
        div(ctx, mk(scrollArea.ent(), 3240),
            ComponentConfig{}
                .with_label("No commands executed yet")
                .with_size(ComponentSize{percent(1.0f), h720(28)})
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(8),
                    .bottom = h720(8), .left = w1280(8)})
                .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("cmdlog_empty"));
        return;
    }

    constexpr float ENTRY_CMD_H = 22.0f;
    constexpr float ENTRY_OUTPUT_H = 18.0f;
    constexpr int MAX_VISIBLE = 200;

    int count = 0;
    for (auto it = cmdLog.entries.rbegin(); it != cmdLog.entries.rend() && count < MAX_VISIBLE; ++it, ++count) {
        auto& entry = *it;
        int entryId = count;

        std::string prefix = entry.success ? "\xe2\x9c\x93 " : "\xe2\x9c\x97 ";
        std::string timeStr = format_timestamp(entry.timestamp);
        std::string cmdLabel = prefix + timeStr + "  " + entry.command;

        afterhours::Color cmdColor = entry.success ? theme::STATUS_ADDED : theme::STATUS_DELETED;

        div(ctx, mk(scrollArea.ent(), 3300 + entryId * 10),
            ComponentConfig{}
                .with_label(cmdLabel)
                .with_size(ComponentSize{percent(1.0f), h720(ENTRY_CMD_H)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_custom_text_color(cmdColor)
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("cmdlog_entry_" + std::to_string(entryId)));

        if (!entry.output.empty()) {
            std::string displayOut = entry.output;
            if (displayOut.size() > 200) {
                displayOut = displayOut.substr(0, 200) + "...";
            }
            for (auto& ch : displayOut) {
                if (ch == '\n') ch = ' ';
                if (ch == '\r') ch = ' ';
            }

            div(ctx, mk(scrollArea.ent(), 3300 + entryId * 10 + 1),
                ComponentConfig{}
                    .with_label(displayOut)
                    .with_size(ComponentSize{percent(1.0f), h720(ENTRY_OUTPUT_H)})
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(8),
                        .bottom = h720(2), .left = w1280(24)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("cmdlog_out_" + std::to_string(entryId)));
        }

        if (!entry.error.empty()) {
            std::string displayErr = entry.error;
            if (displayErr.size() > 200) {
                displayErr = displayErr.substr(0, 200) + "...";
            }
            for (auto& ch : displayErr) {
                if (ch == '\n') ch = ' ';
                if (ch == '\r') ch = ' ';
            }

            div(ctx, mk(scrollArea.ent(), 3300 + entryId * 10 + 2),
                ComponentConfig{}
                    .with_label(displayErr)
                    .with_size(ComponentSize{percent(1.0f), h720(ENTRY_OUTPUT_H)})
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(8),
                        .bottom = h720(2), .left = w1280(24)})
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("cmdlog_err_" + std::to_string(entryId)));
        }
    }
}

} // namespace ecs

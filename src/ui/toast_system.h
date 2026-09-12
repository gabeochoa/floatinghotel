#pragma once

#include "../ui_context.h"
#include "presets.h"
#include <map>

namespace ui {

struct ToastPresentation : afterhours::BaseComponent {
    int repetitions = 1;
};

struct ToastSystem : afterhours::System<ui_imm::UIContextType> {
    void for_each_with(afterhours::Entity&, ui_imm::UIContextType& ctx, float dt) override {
        using namespace afterhours;
        using namespace afterhours::ui;
        using namespace afterhours::ui::imm;
        std::vector<Entity*> pending;
        std::map<std::pair<toast::Level, std::string>, Entity*> distinct;
        for (Entity& entity : EntityQuery({.force_merge = true}).whereHasComponent<toast::Toast>().gen()) {
            auto& notification = entity.get<toast::Toast>();
            if (entity.cleanup || notification.dismissed) { entity.cleanup = true; continue; }
            auto& presentation = entity.addComponentIfMissing<ToastPresentation>();
            const auto& message = entity.get<HasLabel>().label;
            auto [found, inserted] = distinct.emplace(std::pair{notification.level, message}, &entity);
            if (!inserted) {
                auto& first = *found->second;
                first.get<ToastPresentation>().repetitions += presentation.repetitions;
                first.get<toast::Toast>().elapsed = 0.f;
                entity.cleanup = true;
                continue;
            }
            pending.push_back(&entity);
        }
        const float scale = zoom::get();
        const float width = static_cast<float>(graphics::get_screen_width()) / scale;
        const float height = static_cast<float>(graphics::get_screen_height()) / scale;
        const float cardWidth = std::min(420.f, std::max(0.f, width - 24.f));
        const float textWidth = std::max(1.f, cardWidth - 24.f);
        float bottom = height - 38.f;
        auto& root = ui_imm::getUIRootEntity();
        auto& measure = EntityHelper::get_singleton_cmp_enforce<TextMeasureCache>();
        int visible = 0;
        for (Entity* entity : pending) {
            if (visible == 3 || bottom < 100.f || cardWidth < 80.f) break;
            auto& notification = entity->get<toast::Toast>();
            std::string message = entity->get<HasLabel>().label;
            if (message.starts_with("[X] ") || message.starts_with("[!] ")) message.erase(0, 4);
            const float textHeight = std::max(20.f, measure_text_wrapped(measure, message,
                UIComponent::DEFAULT_FONT, 14.f * scale, (textWidth - 10.f) * scale).height / scale + 10.f);
            const float bodyHeight = std::min(textHeight, std::min(140.f, bottom - 84.f));
            const float cardHeight = bodyHeight + 56.f;
            const float x = width - cardWidth - 12.f;
            const float y = bottom - cardHeight;
            const RectangleType hit{x * scale, y * scale, cardWidth * scale, cardHeight * scale};
            const bool hovered = is_mouse_inside(ctx.mouse.pos, hit);
            const bool persistent = notification.level == toast::Level::Error || notification.level == toast::Level::Warning;
            if (!hovered && !persistent) notification.elapsed += dt;
            if (!persistent && notification.elapsed >= std::max(4.f, notification.duration)) {
                entity->cleanup = true;
                continue;
            }
            Color accent = ::theme::TEXT_ACCENT;
            std::string title = "Notice";
            switch (notification.level) {
                case toast::Level::Success: accent = ::theme::STATUS_ADDED; title = "Done"; break;
                case toast::Level::Warning: accent = ::theme::STATUS_MODIFIED; title = "Warning"; break;
                case toast::Level::Error: accent = ::theme::STATUS_DELETED; title = "Error"; break;
                case toast::Level::Custom: accent = notification.custom_color; break;
                case toast::Level::Info: break;
            }
            const int repetitions = entity->get<ToastPresentation>().repetitions;
            if (repetitions > 1) title += " (" + std::to_string(repetitions) + ")";
            auto card = div(ctx, mk(root, 950000 + entity->id), ComponentConfig{}
                .with_size(ComponentSize{pixels(cardWidth), pixels(cardHeight)})
                .with_absolute_position(x, y).with_flex_direction(FlexDirection::Column).with_no_wrap()
                .with_padding(Padding{.top = pixels(12), .right = pixels(12), .bottom = pixels(12), .left = pixels(12)})
                .with_gap(pixels(4)).with_custom_background(::theme::BUTTON_SECONDARY)
                .with_border(accent, pixels(1)).with_corner_radius(6.f)
                .with_rounded_corners(::theme::layout::ROUNDED_CORNERS)
                .with_render_layer(100).with_debug_name("toast_card"));
            auto heading = div(ctx, mk(card.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(28)}).with_flex_direction(FlexDirection::Row)
                .with_render_layer(100));
            div(ctx, mk(heading.ent(), 0), preset::BodyText(title)
                .with_size(ComponentSize{expand(), pixels(28)}).with_custom_text_color(accent)
                .with_render_layer(100).with_debug_name("toast_title"));
            if (button(ctx, mk(heading.ent(), 1), preset::Button("x")
                    .with_size(ComponentSize{pixels(28), pixels(28)})
                    .with_padding(Padding{.left = pixels(0)}).with_render_layer(100)
                    .with_debug_name("toast_dismiss"))) {
                notification.dismiss();
                entity->cleanup = true;
            }
            auto body = div(ctx, mk(card.ent(), 1), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(bodyHeight)})
                .with_overflow(Overflow::Scroll, Axis::Y).with_render_layer(100)
                .with_debug_name("toast_body"));
            div(ctx, mk(body.ent(), 0), preset::BodyText(message)
                .with_size(ComponentSize{percent(1.f), pixels(textHeight)})
                .with_font_size(pixels(14)).with_text_overflow(TextOverflow::Wrap)
                .with_text_inset(5.f)
                .with_render_layer(100).with_debug_name("toast"));
            bottom = y - 8.f;
            ++visible;
        }
    }
};

}

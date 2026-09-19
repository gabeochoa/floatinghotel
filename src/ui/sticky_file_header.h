#pragma once

#include "geometry.h"

namespace ui {

struct StickyFileHeader : afterhours::BaseComponent {
    afterhours::EntityID viewport = -1;
    afterhours::EntityID next = -1;
    float offset = 0.f;
};

struct ResetStickyFileHeaders : afterhours::System<StickyFileHeader> {
    void for_each_with(afterhours::Entity&, StickyFileHeader& header, float) override {
        header.offset = 0.f;
    }
};

struct PinFileHeaders : afterhours::System<StickyFileHeader, afterhours::ui::UIComponent> {
    static void translate(afterhours::Entity& entity, float delta) {
        auto& component = entity.get<afterhours::ui::UIComponent>();
        component.computed_rel[afterhours::ui::Axis::Y] += delta;
        for (const auto id : component.children) {
            auto child = afterhours::ui::UICollectionHolder::getEntityForID(id);
            if (child.valid() && child->has<afterhours::ui::UIComponent>()) translate(child.asE(), delta);
        }
    }

    void for_each_with(afterhours::Entity& entity, StickyFileHeader& header,
                       afterhours::ui::UIComponent& component, float) override {
        using namespace afterhours::ui;
        auto viewport = UICollectionHolder::getEntityForID(header.viewport);
        auto next = UICollectionHolder::getEntityForID(header.next);
        if (!viewport.valid() || !next.valid() || !viewport->has<HasScrollView>() || !next->has<UIComponent>()) return;
        const float naturalY = screen_rect(entity).y - header.offset;
        const float nextY = screen_rect(next.asE()).y -
            (next->has<StickyFileHeader>() ? next->get<StickyFileHeader>().offset : 0.f);
        const float offset = std::max(0.f, std::min(visible_rect(viewport.asE()).y - naturalY,
            nextY - naturalY - component.rect().height));
        translate(entity, offset - header.offset);
        header.offset = offset;
    }
};

}

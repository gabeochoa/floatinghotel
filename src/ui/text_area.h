#pragma once

#include <afterhours/src/plugins/ui/text_input/text_area.h>

namespace ui {

inline auto text_area(auto& ctx, afterhours::ui::imm::EntityParent parent,
                      std::string& text, afterhours::ui::imm::ComponentConfig config = {}) {
    using afterhours::ui::UIComponent;
    using afterhours::ui::UIComponentDebug;
    using afterhours::ui::UICollectionHolder;
    auto result = afterhours::text_input::text_area(ctx, parent, text, config);
    for (const auto fieldId : result.ent().template get<UIComponent>().children) {
        auto field = UICollectionHolder::getEntityForID(fieldId);
        if (!field.valid() || !field->template has<UIComponent>()) continue;
        for (const auto lineId : field->template get<UIComponent>().children) {
            auto line = UICollectionHolder::getEntityForID(lineId);
            if (!line.valid() || !line->template has<UIComponent, UIComponentDebug>() ||
                line->template get<UIComponentDebug>().name_value != "text_area_line") continue;
            line->template get<UIComponent>().desired[afterhours::ui::Axis::X] = afterhours::ui::percent(1.f);
        }
    }
    return result;
}

}

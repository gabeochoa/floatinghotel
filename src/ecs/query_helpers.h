#pragma once

#include "../../vendor/afterhours/src/core/entity_query.h"

namespace ecs {

template <typename T, typename... Filters>
afterhours::Entity* find_singleton_entity() {
    // gen_first() stops on the first match instead of materializing every
    // entity that happens to carry the component.
    afterhours::OptEntity opt = afterhours::EntityQuery({.force_merge = true})
                                    .whereHasComponent<T, Filters...>()
                                    .gen_first();
    return opt ? &opt.asE() : nullptr;
}

template <typename T, typename... Filters>
T* find_singleton() {
    afterhours::Entity* entity = find_singleton_entity<T, Filters...>();
    return entity ? &entity->template get<T>() : nullptr;
}

} // namespace ecs

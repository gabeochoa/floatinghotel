#pragma once

#include "../../vendor/afterhours/src/core/entity_query.h"

namespace ecs {

template <typename T, typename... Filters>
T* find_singleton() {
    auto q = afterhours::EntityQuery({.force_merge = true})
                 .whereHasComponent<T>();
    (q.template whereHasComponent<Filters>(), ...);
    auto results = q.gen();
    if (results.empty()) return nullptr;
    return &results[0].get().template get<T>();
}

template <typename T, typename... Filters>
afterhours::Entity* find_singleton_entity() {
    auto q = afterhours::EntityQuery({.force_merge = true})
                 .whereHasComponent<T>();
    (q.template whereHasComponent<Filters>(), ...);
    auto results = q.gen();
    if (results.empty()) return nullptr;
    return &results[0].get();
}

} // namespace ecs

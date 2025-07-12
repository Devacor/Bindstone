#pragma once

#include "value.hpp"
#include "conversion_registry.hpp"

namespace jai {

// Template specialization for script_value::as<T>() with custom conversions
// This is included after both value.hpp and conversion_registry.hpp are fully defined

namespace detail {

template<typename T>
inline T try_custom_conversion(const script_value& v) {
    auto& registry = conversions::conversion_registry::instance();
    if (registry.has_conversion<T>()) {
        return registry.convert_from_script<T>(v);
    }
    throw runtime_error("No custom conversion available for type");
}

} // namespace detail

} // namespace jai
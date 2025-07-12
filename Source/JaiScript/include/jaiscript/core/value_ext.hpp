#pragma once

#include "value.hpp"
#include "conversion_registry.hpp"

namespace jai {

// Extension methods for script_value that require conversion_registry
// This header should be included after both value.hpp and conversion_registry.hpp

inline void script_value::set_conversion_enabled(bool enabled) {
    conversion_enabled_ = enabled;
}

template<typename T>
inline T script_value_try_conversion(const script_value& v) {
    auto& registry = conversions::conversion_registry::instance();
    if (registry.has_conversion<T>()) {
        return registry.convert_from_script<T>(v);
    }
    throw runtime_error("No conversion registered for type");
}

} // namespace jai
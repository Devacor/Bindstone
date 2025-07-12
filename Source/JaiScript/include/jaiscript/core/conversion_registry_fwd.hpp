#pragma once

#ifndef __JAISCRIPT_CORE_CONVERSION_REGISTRY_FWD_HPP__
#define __JAISCRIPT_CORE_CONVERSION_REGISTRY_FWD_HPP__

#include <jaiscript/jaiscript_fwd.hpp>
#include <functional>
#include <typeindex>

namespace jai {

namespace conversions {

// Forward declaration of the registry
class conversion_registry;

// Type ID wrapper for type-safe registration
struct type_id {
    std::type_index index;
    
    template<typename T>
    static type_id of() {
        return {std::type_index(typeid(T))};
    }
    
    static type_id of_type(const std::type_info& type) {
        return {std::type_index(type)};
    }
    
    bool operator==(const type_id& other) const {
        return index == other.index;
    }
    
    bool operator<(const type_id& other) const {
        return index < other.index;
    }
};

// Hash function for type_id to use in unordered_map
struct type_id_hash {
    std::size_t operator()(const type_id& tid) const {
        return std::hash<std::type_index>()(tid.index);
    }
};

} // namespace conversions

} // namespace jai

#endif // __JAISCRIPT_CORE_CONVERSION_REGISTRY_FWD_HPP__
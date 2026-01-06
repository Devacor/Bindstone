#pragma once

#include <jaiscript/core/type_info.hpp>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <cstdint>

// Forward declarations to avoid circular dependencies
namespace jai {
    class script_value;

    namespace serialization {
        class archive_writer;
        class archive_reader;
    }
}

namespace jai {
namespace serialization {

// Serialization metadata for properties
struct property_metadata {
    std::string name;
    type_info_ptr type;
    bool read_only = false;  // Skip during serialization if true

    // Network flags
    enum network_flags : uint32_t {
        none = 0,
        reliable = 1 << 0,
        unreliable = 1 << 1,
        immediate = 1 << 2,
        compress_position = 1 << 3,
        throttle_100ms = 1 << 4,
        throttle_500ms = 1 << 5
    };
    uint32_t network_flags = network_flags::none;

    // Direct serialization lambdas for runtime fallback (dynamic_binder-registered types)
    // These work with raw void* pointers to avoid needing script_value wrappers
    // save: (const void* cpp_object, archive_writer&) -> saves the property value
    // load: (void* cpp_object, archive_reader&) -> loads the property value
    std::function<void(const void*, archive_writer&)> save;
    std::function<void(void*, archive_reader&)> load;
};

// Class serialization metadata
struct class_metadata {
    std::string class_name;
    uint32_t current_version = 1;
    std::vector<property_metadata> properties;

    // Custom serialization functions (forward-declared types)
    std::function<void(archive_writer&, const script_value&, uint32_t)> custom_save;
    std::function<script_value(archive_reader&, uint32_t)> custom_construct;

    std::vector<std::string> get_property_names() const {
        std::vector<std::string> result;
        result.reserve(properties.size());
        for (const auto& prop : properties) {
            result.push_back(prop.name);
        }
        return result;
    }
};

// Helper for network flags
struct network_flags {
    uint32_t flags;
    explicit network_flags(uint32_t f) : flags(f) {}
};

} // namespace serialization
} // namespace jai

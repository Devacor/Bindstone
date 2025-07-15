#pragma once

#include <jaiscript/core/engine.hpp>
#include "json.hpp"
#include "io.hpp"
#include "containers.hpp"
// Future stdlib modules:
// #include "math.hpp"
// #include "string.hpp"
// #include "array.hpp"
// #include "map.hpp"

namespace jai {
namespace stdlib {

    // Register all standard library functions with an engine
    inline void register_all(engine& engine) {
        // Core functions
        register_json_functions(engine);
        register_io_functions(engine);
        register_container_types(engine);
        
        // Future registrations:
        // register_math_functions(engine);
        // register_string_functions(engine);
        // register_array_functions(engine);
        // register_map_functions(engine);
    }
    
    // Overload for shared_ptr convenience
    inline void register_all(std::shared_ptr<engine> engine) {
        register_all(*engine);
    }

} // namespace stdlib
} // namespace jai
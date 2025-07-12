#pragma once

#include <jaiscript/core/engine.hpp>
#include "json.hpp"
#include "io.hpp"
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
        
        // Future registrations:
        // register_math_functions(engine);
        // register_string_functions(engine);
        // register_array_functions(engine);
        // register_map_functions(engine);
    }

} // namespace stdlib
} // namespace jai
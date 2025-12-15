#pragma once

#include <jaiscript/core/engine.hpp>
#include "json.hpp"
#include "io.hpp"
#include "containers.hpp"
#include "math.hpp"
// Optional: #include "vector.hpp"  // Vec2/Vec3 types - many engines have their own

namespace jai {
namespace stdlib {

    // Register all standard library functions with an engine
    // NOTE: Does NOT register Vec2/Vec3 by default - call register_vector_types() separately if needed
    inline void register_all(engine& engine) {
        // Core functions
        register_json_functions(engine);
        register_io_functions(engine);
        register_container_types(engine);
        register_math_functions(engine);

        // Optional: register_vector_types(engine);  // Vec2/Vec3 with move_towards
    }
    
    // Overload for shared_ptr convenience
    inline void register_all(std::shared_ptr<engine> engine) {
        register_all(*engine);
    }

} // namespace stdlib
} // namespace jai
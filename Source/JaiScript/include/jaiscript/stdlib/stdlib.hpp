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
    inline void register_all(engine& eng_ref) {
        // Core functions
        register_json_functions(eng_ref);
        register_io_functions(eng_ref);
        register_container_types(eng_ref);
        register_math_functions(eng_ref);

        // Optional: register_vector_types(eng_ref);  // Vec2/Vec3 with move_towards
    }
    
    // Overload for shared_ptr convenience
    inline void register_all(std::shared_ptr<engine> eng_ptr) {
        register_all(*eng_ptr);
    }

} // namespace stdlib
} // namespace jai
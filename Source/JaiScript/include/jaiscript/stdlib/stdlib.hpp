#pragma once

#include <jaiscript/core/engine.hpp>
#include "json.hpp"
#include "io.hpp"
#include "containers.hpp"
#include "math.hpp"
#include "strings.hpp"
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
        register_string_functions(eng_ref);

        // Optional: register_vector_types(eng_ref);  // Vec2/Vec3 with move_towards
    }
    
    // Overloads for shared_ptr / raw-pointer convenience (engine::make() returns a
    // shared_ptr; host callbacks often hold engine*) - no deref spelling needed,
    // for register_all and each individual registration alike
    inline void register_all(const std::shared_ptr<engine>& eng_ptr) { register_all(*eng_ptr); }
    inline void register_all(engine* eng_ptr) { register_all(*eng_ptr); }
    inline void register_json_functions(const std::shared_ptr<engine>& e) { register_json_functions(*e); }
    inline void register_json_functions(engine* e) { register_json_functions(*e); }
    inline void register_io_functions(const std::shared_ptr<engine>& e) { register_io_functions(*e); }
    inline void register_io_functions(engine* e) { register_io_functions(*e); }
    inline void register_container_types(const std::shared_ptr<engine>& e) { register_container_types(*e); }
    inline void register_container_types(engine* e) { register_container_types(*e); }
    inline void register_math_functions(const std::shared_ptr<engine>& e) { register_math_functions(*e); }
    inline void register_math_functions(engine* e) { register_math_functions(*e); }
    inline void register_string_functions(const std::shared_ptr<engine>& e) { register_string_functions(*e); }
    inline void register_string_functions(engine* e) { register_string_functions(*e); }

} // namespace stdlib
} // namespace jai
#pragma once

#include <jaiscript/core/engine.hpp>
#include "json.hpp"
#include "io.hpp"
#include "containers.hpp"
#include "strings.hpp"
#include "reflection.hpp"
#include "time.hpp"
// Native math:: vectors and colors are core language values; host adapters are opt-in.

namespace jai {
namespace stdlib {

    // Register all standard library functions with an engine (source/implementation/stdlib/stdlib.cpp)
    void register_all(engine& eng_ref);
    
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
    inline void register_string_functions(const std::shared_ptr<engine>& e) { register_string_functions(*e); }
    inline void register_string_functions(engine* e) { register_string_functions(*e); }

} // namespace stdlib
} // namespace jai
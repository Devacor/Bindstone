#pragma once

#include <jaiscript/core/engine.hpp>
#include <jaiscript/core/class_builder.hpp>

namespace jai {
namespace stdlib {

    // Internal pair class for range-based for loop iteration
    struct script_pair {
        script_value first;
        script_value second;
        
        // We need to provide the engine reference for script_values
        script_pair(const script_value& f, const script_value& s) 
            : first(f), second(s) {}
            
        // Special constructor that preserves references
        script_pair(const script_value& f, script_value&& s) 
            : first(f), second(std::move(s)) {}
            
        // Factory method for creating a pair with a reference to the second value
        static script_pair make_with_reference(const script_value& key, 
                                             script_value* value_ptr,
                                             const std::shared_ptr<environment>& env,
                                             std::weak_ptr<engine> eng) {
            return script_pair(key.clone(), 
                              script_value::make_reference(value_ptr, env, eng));
        }
    };

    inline void register_container_types(engine& engine) {
        // Register the pair type for map iteration
        class_builder<script_pair>(engine, "pair")
            .constructor<script_value, script_value>()
            .property("first", &script_pair::first)
            .property("second", &script_pair::second)
            .build();
    }

} // namespace stdlib
} // namespace jai
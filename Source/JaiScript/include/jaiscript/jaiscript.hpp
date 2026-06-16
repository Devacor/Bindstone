#pragma once

#ifndef __JAISCRIPT_JAISCRIPT_HPP__
#define __JAISCRIPT_JAISCRIPT_HPP__

// Main JaiScript include file
// Include this to use JaiScript in your project
// 
// This header includes all core JaiScript functionality in the correct dependency order.
// Users should include this single header instead of individual headers.

// 1. Forward declarations (no dependencies)
#include "jaiscript_fwd.hpp"

// 2. Basic types and type system (depends on fwd only)
#include "core/types.hpp"
#include "core/type_info.hpp"

// 3. Conversion system (depends on types)
#include "core/conversion_registry_fwd.hpp"
#include "core/conversion_registry.hpp"

// 4. Core value system (depends on types and conversions)
#include "core/value.hpp"

// 5. Binding traits and containers (depends on value)
#include "core/binding_traits.hpp"
#include "core/bound_array.hpp"
#include "core/bound_map.hpp"

// 6. Function binding system (depends on value and containers)
#include "core/function_binder.hpp"

// 7. Execution backends (depends on value)
#include "core/execution_backend.hpp"

// 8. Engine (depends on all above)
#include "core/engine.hpp"

// 9. Class building system (depends on engine) - LAST!
#include "core/dynamic_binder.hpp"

#include "core/static_binder.hpp"
#include "core/template_binder.hpp"

#include "detail/ast.hpp"
#include "detail/lexer.hpp"
#include "detail/parser.hpp"
#include "detail/interpreter.hpp"
#include "detail/interpreter_backend.hpp"

// VM is being refactored - moved to LEGACY_VM
// #include "jvm/bytecode.hpp"
// #include "jvm/compiler.hpp"
// #include "jvm/virtual_machine.hpp"
// #include "jvm/vm_backend.hpp"

#include "serialization/archive_impl.hpp"
#include "serialization/convenience.hpp"

// Version information
#define JAISCRIPT_VERSION_MAJOR 0
#define JAISCRIPT_VERSION_MINOR 1
#define JAISCRIPT_VERSION_PATCH 0

namespace jai {
    
    // Version string
    inline const std::string& version() {
        static const std::string version_string = "0.1.0";
        return version_string;
    }
    
    
} // namespace jai

#endif // __JAISCRIPT_JAISCRIPT_HPP__
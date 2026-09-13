#pragma once

#ifndef __JAISCRIPT_CORE_SCRIPT_NAMESPACE_HPP__
#define __JAISCRIPT_CORE_SCRIPT_NAMESPACE_HPP__

#include <jaiscript/core/value.hpp>
#include <memory>
#include <unordered_map>
#include <vector>

namespace jai {

class function_decl;
class class_definition;
class environment;

// Flat script namespace registry entry ("a::b::c" is one namespace name, no nesting).
// Engine-owned (see engine::script_namespaces()) so namespaces survive across backends.
struct script_namespace_data {
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> functions;  // function_name_id -> overloads
    std::unordered_map<uint64_t, script_value> variables;                                 // variable_name_id -> value
    std::unordered_map<uint64_t, std::shared_ptr<class_definition>> classes;              // class_name_id -> definition
    std::shared_ptr<environment> scope;   // the cells of the script-declared variables, parented on the global environment (detail/namespace_scope.hpp)
};

// Engine-owned namespaces: reflect:: answers and jai:: capabilities cannot be
// supplied by script declarations. math:: stays deliberately OPEN. Flat namespace
// names make "jai::sub" its own name, so reserve both root-qualified prefixes too.
inline bool is_reserved_namespace_name(std::string_view name) {
    return name == "reflect" || name.starts_with("reflect::") ||
        name == "jai" || name.starts_with("jai::");
}

inline error_propagator reserved_namespace_error(uint64_t name_id) {
    return error_propagator{make_error_code(runtime_error_code::type_mismatch),
        "Namespace '{0}' is reserved for the engine (C++-registered only)", name_id};
}

} // namespace jai

#endif // __JAISCRIPT_CORE_SCRIPT_NAMESPACE_HPP__

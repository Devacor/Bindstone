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

// Flat script namespace registry entry ("a::b::c" is one namespace name, no nesting).
// Engine-owned (see engine::script_namespaces()) so namespaces survive across backends.
struct script_namespace_data {
    std::unordered_map<uint64_t, std::vector<std::shared_ptr<function_decl>>> functions;  // function_name_id -> overloads
    std::unordered_map<uint64_t, script_value> variables;                                 // variable_name_id -> value
    std::unordered_map<uint64_t, std::shared_ptr<class_definition>> classes;              // class_name_id -> definition
};

} // namespace jai

#endif // __JAISCRIPT_CORE_SCRIPT_NAMESPACE_HPP__
